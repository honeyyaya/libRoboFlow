# Service SDK 对外接口设计文档

> 对应头文件：`include/robrt/Service/librobrt_service_api.h`  
> 共享头文件：`include/robrt/librobrt_common.h`  
> 目标平台：Linux arm64（设备/边缘侧）  
> 内部实现：封装 WebRTC + 编码器后端。Service 端负责**接收业务采集数据 → 编码/透传/转码 → 发布给订阅端**，并处理 Client 侧的对讲、服务请求等反向消息  
> ABI 策略：纯 C 导出 + Opaque Handle + Getter / Setter，与 Client 同构对齐

---

## 1. 设计总则

与 Client 同构（见 `client_api_design.md` §1），**额外约束**：

| 原则 | 说明 |
|---|---|
| 反向订阅驱动 | 业务层并不主动发流，而是收到 `on_pull_request` 回调后才开始产帧，避免无订阅者时浪费编码资源 |
| push 帧零拷贝意图 | `push_frame` 对象 set_data 时 SDK 立即 copy 或引用后入队，接口返回即可释放原 buffer |
| 编码参数在 Service | 分辨率 / 编码 / 码率 / GOP / RC 等编码器参数**只在 Service 端**设置，Client 端仅能 hint |
| 对讲为反向通道 | Talk 是 Client → Service 方向，在 `connect_cb` 里通过 `on_talk_*` 回调送到业务；业务侧 SDK 不负责播放 |
| 函数命名空间 | 与 Client 做符号隔离：所有 Service 专属函数/类型使用 `librobrt_svc_` 前缀；共享部分（log/signal/license/global_config、video_frame、audio_frame、stream_stats）沿用 `librobrt_` |

---

## 2. 与 Client 的对应关系

```mermaid
graph LR
    subgraph 共享["共享（librobrt_common.h）"]
        S1[log_config]
        S2[signal_config]
        S3[license_config]
        S4[global_config]
        S5[video_frame 只读]
        S6[audio_frame 只读]
        S7[stream_stats 只读]
        S8[错误码 / 枚举 / 内存释放]
    end

    subgraph ClientSDK["Client（librobrt_*）"]
        C1[connect_info]
        C2[connect_cb]
        C3[stream_param hint]
        C4[stream_cb]
        C5[open_stream / close_stream]
        C6[send_notice / service_reply]
        C7[on_video_frame / on_audio_frame]
    end

    subgraph ServiceSDK["Service（librobrt_svc_*）"]
        V1[svc_connect_info]
        V2[svc_connect_cb]
        V3[svc_stream_param 编码器]
        V4[svc_stream_cb]
        V5[svc_create/start/stop/destroy_stream]
        V6[svc_push_video/audio_frame]
        V7[svc_talk_config]
        V8[svc_send_notice / svc_post_topic / svc_service_reply]
        V9[on_pull_request / on_pull_release]
        VA[on_talk_start/stop/audio/video]
        VB[on_service_req]
    end

    共享 --> ClientSDK
    共享 --> ServiceSDK

    C5 -.订阅.-> V9
    C6 <-.业务消息.-> V8
    C7 <-.媒体数据.-> V6
    V8 -.业务消息.-> C2
    VA <-.对讲.-> C6
```

关键点：
- Client 的 `open_stream(index)` 在 Service 侧触发 `on_pull_request(stream_idx)`，业务层可据此惰性启动采集/编码。
- Client 的 `close_stream` / 主动 `disconnect` → Service 侧触发 `on_pull_release(stream_idx)`。
- Client 的 `send_notice` / `service_reply` → Service 侧 `on_notice` / 业务对 `on_service_req` 的回复反向亦然。
- 对讲方向：Client 采集 → 通过 Client 端内部通道 → Service 侧 `on_talk_audio` / `on_talk_video` 回调给业务播放。

---

## 3. 对象生命周期（状态机）

### 3.1 SDK 全局

```mermaid
stateDiagram-v2
    [*] --> UNINIT
    UNINIT --> CONFIGURED: svc_set_global_config / svc_set_talk_config
    CONFIGURED --> INITED: svc_init()
    UNINIT --> INITED: svc_init() [默认配置]
    INITED --> CONNECTED: svc_connect() 成功
    INITED --> INITED: svc_connect() 失败
    CONNECTED --> INITED: svc_disconnect() / 被踢
    INITED --> UNINIT: svc_uninit()
    CONNECTED --> UNINIT: svc_uninit() [内部先 disconnect]
```

### 3.2 单路流

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> CREATED: svc_create_stream()
    CREATED --> RUNNING: svc_start_stream()
    RUNNING --> CREATED: svc_stop_stream()
    CREATED --> DESTROYED: svc_destroy_stream()
    RUNNING --> DESTROYED: svc_destroy_stream() [内部先 stop]
    DESTROYED --> [*]
    note right of RUNNING
        RUNNING 状态下：
        - 允许 push_video_frame / push_audio_frame
        - 订阅方可收到媒体
    end note
```

### 3.3 绑定状态

```mermaid
stateDiagram-v2
    [*] --> UNBOUND
    UNBOUND --> BOUND: 云端注册成功
    UNBOUND --> FAILED: 鉴权/license 失败
    BOUND --> UNBOUND: 云端解绑
    FAILED --> [*]
```

---

## 4. 标准调用时序

### 4.1 完整流程（启动 → 懒启动流 → 被 Client 订阅 → 停止）

```mermaid
sequenceDiagram
    autonumber
    participant BIZ as 业务层
    participant SDK as librobrt_svc
    participant CLI as 远端 Client

    Note over BIZ,CLI: ---- 阶段 1：配置 + 初始化 ----
    BIZ->>SDK: log/signal/license/global_config 构造
    BIZ->>SDK: svc_set_global_config(g)
    BIZ->>SDK: svc_talk_config_create / set_audio / set_video
    BIZ->>SDK: svc_set_talk_config(tc)
    BIZ->>SDK: svc_init()
    SDK-->>BIZ: OK

    Note over BIZ,CLI: ---- 阶段 2：连接 ----
    BIZ->>SDK: svc_connect_info_create / set_device_id / secret
    BIZ->>SDK: svc_connect_cb_create / set_on_pull_request / ...
    BIZ->>SDK: svc_connect(info, cb)
    SDK-->>BIZ: on_connect_state(CONNECTED)
    SDK-->>BIZ: on_bind_state(BOUND)

    Note over BIZ,CLI: ---- 阶段 3：Client 订阅 → 懒启动 ----
    CLI->>SDK: (open_stream idx=0)
    SDK-->>BIZ: on_pull_request(stream_idx=0)
    BIZ->>SDK: svc_stream_param_create / set_in_codec / set_out_codec / set_src_size / ...
    BIZ->>SDK: svc_create_stream(0, param, NULL, &h0)
    BIZ->>SDK: svc_start_stream(h0)

    loop 媒体循环
        BIZ->>SDK: svc_push_frame_create / set_codec / set_data / set_pts
        BIZ->>SDK: svc_push_video_frame(h0, pf)
        BIZ->>SDK: svc_push_frame_destroy(pf)
        SDK->>CLI: (编码后帧)
    end
    SDK-->>BIZ: on_stream_stats(h0, stats)

    Note over BIZ,CLI: ---- 阶段 4：Client 退订 → 惰性停止 ----
    CLI->>SDK: (close_stream)
    SDK-->>BIZ: on_pull_release(stream_idx=0)
    BIZ->>SDK: svc_stop_stream(h0)
    BIZ->>SDK: svc_destroy_stream(h0)

    Note over BIZ,CLI: ---- 阶段 5：退出 ----
    BIZ->>SDK: svc_disconnect()
    SDK-->>BIZ: on_connect_state(DISCONNECTED)
    BIZ->>SDK: svc_uninit()
    SDK-->>BIZ: OK
```

### 4.2 业务异常路径：直接 uninit（幂等强清理）

```mermaid
sequenceDiagram
    autonumber
    participant BIZ as 业务层
    participant SDK as librobrt_svc

    BIZ->>SDK: svc_init()
    BIZ->>SDK: svc_connect(...)
    BIZ->>SDK: svc_create_stream(0) → h0
    BIZ->>SDK: svc_start_stream(h0)

    Note over BIZ: 业务进程异常，直接 uninit

    BIZ->>SDK: svc_uninit()
    SDK->>SDK: 内部 stop_stream(h0) + destroy_stream(h0)
    SDK-->>BIZ: on_stream_state(h0, CLOSED, UNINIT)
    SDK->>SDK: 内部 disconnect
    SDK-->>BIZ: on_connect_state(DISCONNECTED)
    SDK->>SDK: 释放全局资源
    SDK-->>BIZ: OK
```

### 4.3 对讲反向流：Client 发起对讲，Service 接收音频

```mermaid
sequenceDiagram
    autonumber
    participant CLI as 远端 Client
    participant SDK as librobrt_svc
    participant BIZ as 业务层（Service 侧）
    participant SPK as 业务播放器

    CLI->>SDK: (start talk, idx=0)
    SDK-->>BIZ: on_talk_start(stream_idx=0)
    BIZ->>SPK: 准备扬声器

    loop 对讲循环
        CLI->>SDK: (audio frame)
        SDK-->>BIZ: on_talk_audio(idx=0, frame)
        BIZ->>SDK: librobrt_audio_frame_get_data / get_pts_ms
        BIZ->>SPK: 送扬声器播放
        Note over BIZ: 回调返回后 frame 失效<br/>如需异步播放链路先 retain
    end

    CLI->>SDK: (stop talk)
    SDK-->>BIZ: on_talk_stop(stream_idx=0)
    BIZ->>SPK: 关闭扬声器
```

### 4.4 Client 服务请求 → Service 异步回包

```mermaid
sequenceDiagram
    autonumber
    participant CLI as 远端 Client
    participant SDK as librobrt_svc
    participant BIZ as 业务回调线程
    participant WRK as 业务工作线程

    CLI->>SDK: service_request("SetPTZ", payload)
    SDK-->>BIZ: on_service_req(req_id=7, "SetPTZ", payload, len)
    BIZ->>WRK: 投递到工作线程
    Note over BIZ,SDK: 回调内不得阻塞，立即返回
    WRK-->>WRK: 调用 PTZ 驱动
    WRK->>SDK: svc_service_reply(req_id=7, OK, resp, resp_len)
    SDK->>CLI: service_response(resp)
```

### 4.5 订阅端切换 / 引用计数式懒启动

```mermaid
sequenceDiagram
    autonumber
    participant C1 as Client A
    participant C2 as Client B
    participant SDK as librobrt_svc
    participant BIZ as 业务层

    C1->>SDK: open_stream(0)
    SDK-->>BIZ: on_pull_request(0)
    BIZ->>SDK: create_stream(0) + start_stream
    Note over SDK: 内部订阅者计数 = 1

    C2->>SDK: open_stream(0)
    Note over SDK: 订阅者计数 = 2<br/>已有编码流，不再回调 on_pull_request

    C1->>SDK: close_stream(0)
    Note over SDK: 订阅者计数 = 1，仍保留编码

    C2->>SDK: close_stream(0)
    Note over SDK: 订阅者计数 = 0
    SDK-->>BIZ: on_pull_release(0)
    BIZ->>SDK: stop_stream + destroy_stream
```

### 4.6 分片推送大帧（push_frame_set_flush / offset）

```mermaid
sequenceDiagram
    autonumber
    participant BIZ as 业务层
    participant SDK as librobrt_svc

    Note over BIZ: 一帧 H264 I 帧 = 1.8MB，分 3 片

    BIZ->>SDK: push_frame set_data(片1) set_flush(false) set_offset(0)
    BIZ->>SDK: svc_push_video_frame(h, pf)
    BIZ->>SDK: push_frame set_data(片2) set_flush(false) set_offset(600KB)
    BIZ->>SDK: svc_push_video_frame(h, pf)
    BIZ->>SDK: push_frame set_data(片3) set_flush(true) set_offset(1200KB)
    BIZ->>SDK: svc_push_video_frame(h, pf)
    Note over SDK: 收到 flush=true 后组帧并送编码器
```

---

## 5. 线程与并发模型

```mermaid
flowchart TB
    subgraph 调用线程["业务线程（可多个）"]
        U1[svc_init / svc_uninit]
        U2[svc_connect / svc_disconnect]
        U3[svc_create / start / stop / destroy_stream]
        U4[svc_push_video_frame / svc_push_audio_frame]
        U5[svc_send_notice / svc_service_reply]
    end

    subgraph SDK内部["SDK 内部线程池"]
        T1[Signal]
        T2[Media TX 编码+发送]
        T3[Media RX 对讲]
        T4[Worker]
    end

    subgraph 回调目标["回调在 SDK 内部线程触发"]
        CB1[on_connect_state / on_bind_state]
        CB2[on_pull_request / on_pull_release]
        CB3[on_service_req / on_notice]
        CB4[on_talk_start/stop/audio/video]
        CB5[on_stream_state / on_encoded_video / on_stream_stats]
        CB6[on_log]
    end

    U1 -.串行.-> SDK内部
    U2 -.串行.-> T1
    U3 -.线程安全.-> T2
    U4 -.线程安全.-> T2
    U5 -.线程安全.-> T1

    T1 --> CB1
    T1 --> CB2
    T1 --> CB3
    T3 --> CB4
    T2 --> CB5
    T4 --> CB6

    CB2 -.白名单 API.-> U3
    CB3 -.白名单 API.-> U5
```

约束同 Client（见 `client_api_design.md` §5）：全局 API 串行、push/流控制 API 线程安全、回调内禁阻塞。

---

## 6. 内存所有权矩阵

| 对象 | 分配方 | 释放方 | 生命周期 |
|---|---|---|---|
| `librobrt_*_config_t`（共享 log/signal/license/global） | SDK `create` | 调用方 `destroy` | `svc_set_global_config` 返回后可销毁 |
| `librobrt_svc_connect_info_t` / `connect_cb_t` / `stream_param_t` / `stream_cb_t` / `talk_config_t` / `push_frame_t` | SDK `svc_*_create` | 调用方 `svc_*_destroy` | 对应 API 返回后可销毁 |
| `librobrt_svc_stream_handle_t` | SDK `create_stream` | SDK `destroy_stream` / `uninit` | 收到 `CLOSED/DESTROYED` 后失效 |
| `librobrt_video_frame_t` / `librobrt_audio_frame_t`（回调入参） | SDK | SDK（回调返回时） | 仅回调栈内；`retain` 后转为调用方管理 |
| `librobrt_stream_stats_t`（回调/pull 入参） | SDK | SDK | 仅调用栈内 |
| `librobrt_svc_license_info_t` | SDK（`get_license_info` 出参） | 调用方 `svc_license_info_destroy` | 显式生命周期 |
| push_frame 里 set_data 的 buffer | 调用方 | 调用方 | SDK 内部 copy，API 返回后可释放 |

---

## 7. 错误处理

```mermaid
flowchart LR
    A[svc_* API] --> B{返回值}
    B -->|ROBRT_OK| S[成功]
    B -->|非 0| E[失败]
    E --> F[librobrt_get_last_error]
    F --> G[thread-local 文本]
    E --> H{分段}
    H -->|0x0xxx| H1[通用]
    H -->|0x1xxx| H2[连接：auth/license/kicked/network]
    H -->|0x2xxx| H3[流：codec_unsupp / encoder_fail / no_subscriber]
```

Service 专属错误码扩展（位于 0x2xxx 段）：
- `ROBRT_ERR_STREAM_ENCODER_FAIL` — 编码器初始化/编码失败
- `ROBRT_ERR_STREAM_NO_SUBSCRIBER` — 无订阅者时尝试某些只读状态操作

---

## 8. ABI 兼容性保证

与 Client 同策略（见 `client_api_design.md` §8）。

**补充**：
- `push_frame` 将来如需新增字段（如 HDR / 时间戳基准）→ 追加 `svc_push_frame_set_xxx`；旧调用方不受影响。
- `stream_param` 加新编码参数 → 追加 `svc_stream_param_set_xxx`。
- 新增回调事件 → 追加 `svc_connect_cb_set_on_xxx`。

---

## 9. 快速上手示例（C）

```c
#include "robrt/Service/librobrt_service_api.h"
#include <stdio.h>
#include <string.h>

static librobrt_svc_stream_handle_t g_h0 = NULL;

static void on_pull_req(int32_t idx, void *ud) {
    if (idx != 0 || g_h0) return;

    librobrt_svc_stream_param_t p = librobrt_svc_stream_param_create();
    librobrt_svc_stream_param_set_in_codec (p, ROBRT_CODEC_NV12);
    librobrt_svc_stream_param_set_out_codec(p, ROBRT_CODEC_H264);
    librobrt_svc_stream_param_set_src_size (p, 1920, 1080);
    librobrt_svc_stream_param_set_out_size (p, 1920, 1080);
    librobrt_svc_stream_param_set_fps      (p, 30);
    librobrt_svc_stream_param_set_gop      (p, 60);
    librobrt_svc_stream_param_set_rc_mode  (p, ROBRT_RC_CBR);
    librobrt_svc_stream_param_set_bitrate  (p, 4000, 6000);

    librobrt_svc_create_stream(idx, p, NULL, &g_h0);
    librobrt_svc_stream_param_destroy(p);
    librobrt_svc_start_stream(g_h0);
}

static void on_pull_rel(int32_t idx, void *ud) {
    if (idx != 0 || !g_h0) return;
    librobrt_svc_stop_stream(g_h0);
    librobrt_svc_destroy_stream(g_h0);
    g_h0 = NULL;
}

int main(void) {
    librobrt_global_config_t g = librobrt_global_config_create();
    librobrt_svc_set_global_config(g);
    librobrt_global_config_destroy(g);

    librobrt_svc_init();

    librobrt_svc_connect_info_t info = librobrt_svc_connect_info_create();
    librobrt_svc_connect_info_set_device_id    (info, "dev-001");
    librobrt_svc_connect_info_set_device_secret(info, "secret-xxx");

    librobrt_svc_connect_cb_t cb = librobrt_svc_connect_cb_create();
    librobrt_svc_connect_cb_set_on_pull_request(cb, on_pull_req);
    librobrt_svc_connect_cb_set_on_pull_release(cb, on_pull_rel);

    librobrt_svc_connect(info, cb);
    librobrt_svc_connect_info_destroy(info);
    librobrt_svc_connect_cb_destroy(cb);

    /* 假设上层每 33ms 推一帧 */
    /* while (running) {
           librobrt_svc_push_frame_t pf = librobrt_svc_push_frame_create();
           librobrt_svc_push_frame_set_codec  (pf, ROBRT_CODEC_NV12);
           librobrt_svc_push_frame_set_size   (pf, 1920, 1080);
           librobrt_svc_push_frame_set_data   (pf, yuv_buf, yuv_len);
           librobrt_svc_push_frame_set_pts_ms (pf, now_ms);
           librobrt_svc_push_video_frame(g_h0, pf);
           librobrt_svc_push_frame_destroy(pf);
       }
    */

    librobrt_svc_disconnect();
    librobrt_svc_uninit();
    return 0;
}
```

---

## 10. 设计关键决策说明

| 决策 | 理由 |
|---|---|
| **懒启动（on_pull_request 驱动）** | 避免业务一直采集/编码浪费 CPU；多订阅者共用一路编码流 |
| **push_frame 对象化** | 字段随编码格式演进会不断增加（HDR、颜色空间、时间基），用对象 + setter 避免长参数列表与签名破坏 |
| **svc_ 前缀** | 同一进程理论可同时加载 Client/Service（例如远程办公工具），避免符号碰撞 |
| **共享 video_frame / audio_frame / stream_stats** | 回调消费端逻辑一致，两端共享 getter 能减小集成认知成本 |
| **talk_config 独立 setter** | 对讲能力由业务决定（决定了能解什么码率/格式），与主媒体参数解耦 |
| **license_info 为 opaque + getter** | license 字段未来必然扩展（到期时间、配额、功能开关等），避免结构体暴露导致的大小变更 |
| **post_topic 作为透传通道** | 高频状态（心率/电量/PTZ 位置等）走透传不经业务层信令语义，减少业务实现压力 |

---

## 11. 落地对照

| 评审项（来自 `client_api_design_review.md` Service 映射） | 状态 |
|---|---|
| 不暴露结构体，opaque + get/set | ✅ |
| 编码参数仅在 Service | ✅ `svc_stream_param_*` |
| push_frame 对象化 | ✅ 对象 + 分片 flush/offset |
| 懒启动回调 | ✅ `on_pull_request` / `on_pull_release` |
| 对讲反向通道 | ✅ `on_talk_start/stop/audio/video` + `svc_talk_config` |
| 异步回包 | ✅ `svc_service_reply(req_id, ...)` |
| 幂等强清理 | ✅ `disconnect` / `uninit` / `destroy_stream` |
| 共享 common.h | ✅ 错误码 / 枚举 / 共享 config / 共享 frame getter |
| 运行期切 URL / license / 动态镜头数 | ❌ 按决策不做 |


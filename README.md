# ComVideoSDK 文档

## 架构一览

```
ComVideoSDK
├── CMakeLists.txt                       # 顶层构建入口
├── include/
│   └── rflow/
│       ├── librflow_version.h           # 版本号宏
│       ├── librflow_typedef.h           # 平台/导出宏/基础类型
│       ├── librflow_common.h            # Client & Service 共享：错误码/枚举/共享 opaque
│       ├── Client/
│       │   └── librflow_client_api.h
│       └── Service/
│           └── librflow_service_api.h
├── src/
│   ├── common/                          # 共享 opaque 对象的 C ABI 胶水层
│   │   ├── internal/                    # 内部 struct 定义（opaque 真身）
│   │   ├── log/                         # log_config / logger
│   │   ├── config/                      # signal_config / license_config / global_config
│   │   ├── frame/                       # video_frame / stream_stats
│   │   ├── error/                       # last_error (thread-local)
│   │   └── util/                        # memory_free / version
│   ├── core/                            # 内部核心（当前为 stub，待接入 libwebrtc）
│   │   ├── rtc/                         # WebRTC 封装
│   │   ├── signal/                      # 信令通道
│   │   └── thread/                      # 线程池
│   ├── client/                          # Android arm64 → librflow_client.so
│   │   ├── CMakeLists.txt
│   │   └── internal/                    # Client 专属 opaque 真身 + 全局 state
│   └── service/                         # Linux arm64 → librflow_svc.so
│       ├── CMakeLists.txt
│       └── internal/                    # Service 专属 opaque 真身 + 全局 state
├── docs/
│   ├── README.md                        # 本文件
│   ├── client_api_design_review.md      # Client 接口初版评审
│   ├── client_api_design.md             # Client 最终设计（含时序图/状态机）
│   └── service_api_design.md            # Service 最终设计（含时序图/状态机）
└── script/                              # 构建/打包脚本（预留）
```

## 构建

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DRFLOW_BUILD_CLIENT=ON \
    -DRFLOW_BUILD_SERVICE=ON
cmake --build build -j
```

产物：
- `build/src/client/librflow_client.so` — Client（Android arm64）
- `build/src/service/librflow_svc.so`   — Service（Linux arm64）

常用选项：

| 选项 | 默认 | 含义 |
|---|---|---|
| `RFLOW_BUILD_CLIENT`  | ON  | 生成 `librflow_client` |
| `RFLOW_BUILD_SERVICE` | ON  | 生成 `librflow_svc` |
| `RFLOW_BUILD_SHARED`  | ON  | `SHARED=.so` / `OFF=.a` |
| `RFLOW_ENABLE_WERROR` | OFF | `-Werror` |
| `RFLOW_CLIENT_ENABLE_WEBRTC_IMPL`  | OFF | 将 WebRTC 拉流实现链入 `librflow_client`（需 `dependences/lib/<plat>/<arch>/libwebrtc.a`） |
| `RFLOW_SERVICE_ENABLE_WEBRTC_IMPL` | OFF | 将 WebRTC 推流实现链入 `librflow_svc`（同上）；开启后 `librflow_svc_push_video_frame` 才真正工作 |
| `RFLOW_ENABLE_ROCKCHIP_MPP`        | OFF | Service 侧启用 Rockchip MPP 硬件编解码 |

### 打开完整推拉流（示例：Linux arm64）

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DRFLOW_BUILD_CLIENT=ON \
    -DRFLOW_BUILD_SERVICE=ON \
    -DRFLOW_CLIENT_ENABLE_WEBRTC_IMPL=ON \
    -DRFLOW_SERVICE_ENABLE_WEBRTC_IMPL=ON
cmake --build build -j

# 产物：
#   build/src/client/librflow_client.so
#   build/src/service/librflow_svc.so
#   build/apps/signaling_server
#   build/apps/push_demo_sdk      (基于 C ABI 的推流 demo)
#   build/apps/pull_demo_sdk      (基于 C ABI 的拉流 demo)
```

## SDK 端到端联调（push_demo_sdk + pull_demo_sdk）

```bash
# 1. 启动信令服务
./build/apps/signaling_server --host 0.0.0.0 --port 8765 &

# 2. Service 端：推送合成 I420 帧（device_id=dev1, stream_idx=1, 640x360@30）
./build/apps/push_demo_sdk 127.0.0.1:8765 dev1 640 360 30 &

# 3. Client 端：订阅 dev1 的 stream_idx=1
./build/apps/pull_demo_sdk 127.0.0.1:8765 dev1 1
```

业务要把自己的相机/编码器帧喂进 SDK 时，参考 `apps/push_demo_sdk.cpp` 中的 `librflow_svc_push_frame_*` 调用即可（当前支持 `RFLOW_CODEC_I420` / `RFLOW_CODEC_NV12`；其它像素格式先在业务侧转成 I420）。

## 角色分工

| 端 | 运行平台 | 动作 | 对应 SDK 函数前缀 |
|---|---|---|---|
| **Client**  | Android arm64（native C++，不经 JVM/Context） | 订阅拉流 → 解码前/后帧回调 | `librflow_` |
| **Service** | Linux arm64（设备/边缘） | 采集/编码/转码 → 发布订阅端 | `librflow_svc_` |

共享部分（`log_config` / `signal_config` / `license_config` / `global_config` / `video_frame` / `stream_stats` / 内存释放 / 错误与版本 API）前缀统一为 `librflow_`，两端库各自实现导出。

## ABI 策略

1. 全部对外类型为 **opaque 指针**：`typedef struct xxx_s* xxx_t;`
2. 配置对象：`create` / `destroy` + `set_*` / `get_*`
3. 回调信息对象（frame / stats）：**只读 opaque 句柄 + `get_*`**，栈内有效；跨栈持有用 `retain` / `release`
4. 错误码分段：通用 `0x0xxx` / 连接 `0x1xxx` / 流 `0x2xxx`；错误文本走 **thread-local** `librflow_get_last_error()`
5. 枚举 **只增不改**；函数签名 **只增不改**；添加能力用新函数名
6. 每个 opaque 结构体在内部实现中带 `magic` 字段，便于野指针检测（见 `src/common/internal/handle.h`）

## 生命周期

### Client

```
set_global_config → init → connect → open_stream* → close_stream* → disconnect → uninit
```
`disconnect` / `uninit` / `close_stream` 幂等强清理。

### Service

```
svc_set_global_config → svc_init → svc_connect →
  [on_pull_request → svc_create_stream → svc_start_stream → svc_push_*_frame*] →
  svc_stop_stream → svc_destroy_stream → svc_disconnect → svc_uninit
```
`svc_disconnect` / `svc_uninit` / `svc_destroy_stream` 幂等强清理。

## 详细设计

- Client：见 [client_api_design.md](./client_api_design.md)
- Service：见 [service_api_design.md](./service_api_design.md)
- 早期评审纪要：见 [client_api_design_review.md](./client_api_design_review.md)

## 实现现状

- ABI 层全部落地：`*_create` / `*_destroy` / `*_set_*` / `*_get_*` 与所有对外函数符号齐备。
- **Service 推流（`librflow_svc_push_video_frame`）已打通**（需 `RFLOW_SERVICE_ENABLE_WEBRTC_IMPL=ON`）：
  - `ExternalPushVideoTrackSource`（`src/service/impl/media/external_push_video_track_source.h/.cpp`）：
    `AdaptedVideoTrackSource` 子类，`PushI420/PushNv12` 把业务帧广播给 WebRTC 编码链路。
  - `PushStreamer::use_external_video_source`：跳过 V4L2 直采，改用外部源。
  - `Publisher`（`src/service/impl/publisher.{h,cpp}`）：桥接 C ABI ↔ `PushStreamer` + `SignalingClient`，
    负责把 `subscriber_join`→`on_pull_request`、`CreateOfferForPeer` 主线程化、SDP/ICE 双向路由。
  - `librflow_svc_create_stream` / `start_stream` / `push_video_frame` / `stop_stream` / `destroy_stream`
    全部走 `Publisher`，`librflow_svc_connect_cb_set_on_pull_request/release` 被实际触发。
- **Client 拉流**（`librflow_open_stream` + `on_video_frame`）在 `RFLOW_CLIENT_ENABLE_WEBRTC_IMPL=ON` 时
  由 `src/client/impl/rtc_stream/*` 提供。
- 仍为 stub 的部分（不影响端到端推拉流）：
  - `librflow_svc_connect` 不做云端鉴权/license 校验，直接 `CONNECTED` + `BIND_BOUND`（`src/service/lifecycle.cpp`）。
  - `librflow_stream_get_stats` / `librflow_svc_stream_get_stats` 返回 `NOT_SUPPORT`。
  - Client/Service 两侧信令客户端尚未合并（`src/client/impl/signaling/` vs `src/service/impl/signaling/`）。
- 线程池为内置最简实现，暂不依赖第三方 executor。

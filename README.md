# ComVideoSDK 文档

## 架构一览

```
ComVideoSDK
├── CMakeLists.txt                       # 顶层构建入口
├── include/
│   └── robrt/
│       ├── librobrt_version.h           # 版本号宏
│       ├── librobrt_typedef.h           # 平台/导出宏/基础类型
│       ├── librobrt_common.h            # Client & Service 共享：错误码/枚举/共享 opaque
│       ├── Client/
│       │   └── librobrt_client_api.h
│       └── Service/
│           └── librobrt_service_api.h
├── src/
│   ├── common/                          # 共享 opaque 对象的 C ABI 胶水层
│   │   ├── internal/                    # 内部 struct 定义（opaque 真身）
│   │   ├── log/                         # log_config / logger
│   │   ├── config/                      # signal_config / license_config / global_config
│   │   ├── frame/                       # video_frame / audio_frame / stream_stats
│   │   ├── error/                       # last_error (thread-local)
│   │   └── util/                        # memory_free / version
│   ├── core/                            # 内部核心（当前为 stub，待接入 libwebrtc）
│   │   ├── rtc/                         # WebRTC 封装
│   │   ├── signal/                      # 信令通道
│   │   └── thread/                      # 线程池
│   ├── client/                          # Android arm64 → librobrt_client.so
│   │   ├── CMakeLists.txt
│   │   └── internal/                    # Client 专属 opaque 真身 + 全局 state
│   └── service/                         # Linux arm64 → librobrt_svc.so
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
    -DROBRT_BUILD_CLIENT=ON \
    -DROBRT_BUILD_SERVICE=ON
cmake --build build -j
```

产物：
- `build/src/client/librobrt_client.so` — Client（Android arm64）
- `build/src/service/librobrt_svc.so`   — Service（Linux arm64）

常用选项：

| 选项 | 默认 | 含义 |
|---|---|---|
| `ROBRT_BUILD_CLIENT`  | ON  | 生成 `librobrt_client` |
| `ROBRT_BUILD_SERVICE` | ON  | 生成 `librobrt_svc` |
| `ROBRT_BUILD_SHARED`  | ON  | `SHARED=.so` / `OFF=.a` |
| `ROBRT_ENABLE_WERROR` | OFF | `-Werror` |

## 角色分工

| 端 | 运行平台 | 动作 | 对应 SDK 函数前缀 |
|---|---|---|---|
| **Client**  | Android arm64（native C++，不经 JVM/Context） | 订阅拉流 → 解码前/后帧回调 | `librobrt_` |
| **Service** | Linux arm64（设备/边缘） | 采集/编码/转码 → 发布 + 反向对讲接收 | `librobrt_svc_` |

共享部分（`log_config` / `signal_config` / `license_config` / `global_config` / `video_frame` / `audio_frame` / `stream_stats` / 内存释放 / 错误与版本 API）前缀统一为 `librobrt_`，两端库各自实现导出。

## ABI 策略

1. 全部对外类型为 **opaque 指针**：`typedef struct xxx_s* xxx_t;`
2. 配置对象：`create` / `destroy` + `set_*` / `get_*`
3. 回调信息对象（frame / stats）：**只读 opaque 句柄 + `get_*`**，栈内有效；跨栈持有用 `retain` / `release`
4. 错误码分段：通用 `0x0xxx` / 连接 `0x1xxx` / 流 `0x2xxx`；错误文本走 **thread-local** `librobrt_get_last_error()`
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
svc_set_global_config → svc_set_talk_config → svc_init → svc_connect →
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
- 核心业务逻辑（WebRTC / 编码器 / 信令 IO）以 stub 占位，标记 `TODO`，后续按 Issue 接入 `libwebrtc` 等依赖：
  - `src/core/rtc/rtc_stub.cpp`
  - `src/core/signal/signal_stub.cpp`
  - `src/client/lifecycle.cpp`（connect 处 TODO）
  - `src/service/stream.cpp`（push_video_frame 处 TODO）
- 线程池为内置最简实现，暂不依赖第三方 executor。

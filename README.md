# ComVideoSDK 文档

## 目录结构

```text
ComVideoSDK
├── CMakeLists.txt
├── include/
│   └── rflow/
│       ├── librflow_version.h
│       ├── librflow_typedef.h
│       ├── librflow_common.h
│       ├── Client/
│       │   └── librflow_client_api.h
│       └── Service/
│           └── librflow_service_api.h
├── src/
│   ├── common/
│   ├── core/
│   ├── client/
│   └── service/
├── apps/
├── docs/
└── scripts/
```

## 构建

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DRFLOW_BUILD_CLIENT=ON \
    -DRFLOW_BUILD_SERVICE=ON
cmake --build build -j
```

默认产物：

- `build/src/client/librflow_client.so`
- `build/src/service/librflow_svc.so`

常用选项：

| 选项 | 默认 | 含义 |
|---|---|---|
| `RFLOW_BUILD_CLIENT` | `ON` | 构建 `librflow_client` |
| `RFLOW_BUILD_SERVICE` | `ON` | 构建 `librflow_svc` |
| `RFLOW_BUILD_SHARED` | `ON` | `ON` 生成 `.so`，`OFF` 生成静态库 |
| `RFLOW_ENABLE_WERROR` | `OFF` | 开启 `-Werror` |
| `RFLOW_CLIENT_ENABLE_WEBRTC_IMPL` | `OFF` | 启用 Client 侧 WebRTC 拉流实现 |
| `RFLOW_SERVICE_ENABLE_WEBRTC_IMPL` | `OFF` | 启用 Service 侧 WebRTC 推流实现；开启后可使用 SDK 内部采集推流，或使用 `librflow_svc_push_video_frame` 走业务侧外部投帧 |
| `RFLOW_ENABLE_ROCKCHIP_MPP` | `OFF` | 启用 Rockchip MPP 硬件编解码 |
| `RFLOW_BUILD_APPS` | `OFF` | 构建 `apps/` 下的 demo 和信令服务 |

### 打开完整推拉流能力

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DRFLOW_BUILD_CLIENT=ON \
    -DRFLOW_BUILD_SERVICE=ON \
    -DRFLOW_BUILD_APPS=ON \
    -DRFLOW_CLIENT_ENABLE_WEBRTC_IMPL=ON \
    -DRFLOW_SERVICE_ENABLE_WEBRTC_IMPL=ON
cmake --build build -j
```

典型产物：

- `build/src/client/librflow_client.so`
- `build/src/service/librflow_svc.so`
- `build/apps/signaling_server`
- `build/apps/push_demo_sdk`
- `build/apps/pull_demo_sdk`

## SDK 端到端联调

```bash
# 1. 启动信令服务
./build/apps/signaling_server --host 0.0.0.0 --port 8765 &

# 2. Service 端：由 SDK 内部采集 /dev/video0 并推流
./build/apps/push_demo_sdk 127.0.0.1:8765 dev1 640 360 30 0 /dev/video0 &

# 3. Client 端：订阅 dev1 的 stream_idx=0
./build/apps/pull_demo_sdk 127.0.0.1:8765 dev1 0
```

说明：

- `apps/push_demo_sdk.cpp` 现在演示的是“只调用 SDK，由 SDK 内部完成采集、编码、推流”的模式。
- Linux 下可通过命令行最后一个参数指定相机路径，也可用环境变量 `RFLOW_PUSH_DEMO_CAMERA=/dev/videoX` 指定；未指定时默认 `/dev/video0`。
- 如果业务需要自己喂帧给 SDK，仍可使用 `librflow_svc_push_frame_*` + `librflow_svc_push_video_frame`。
- 当前外部投帧支持 `RFLOW_CODEC_I420` 和 `RFLOW_CODEC_NV12`；其它像素格式需先在业务侧转换。
- 当流已经配置 `video_device_path` 或 `video_device_index` 使用 SDK 内部采集时，不应再对同一流调用 `librflow_svc_push_video_frame`。

## 角色划分

| 端 | 平台 | 职责 | API 前缀 |
|---|---|---|---|
| Client | Android arm64 / native C++ | 拉流、解码、回调业务层 | `librflow_` |
| Service | Linux arm64 / 设备侧 | 采集、编码、转码、发布流 | `librflow_svc_` |

共享对象如 `log_config`、`signal_config`、`license_config`、`global_config`、`video_frame`、`stream_stats` 等统一走 `librflow_` 前缀。

## ABI 约定

1. 对外句柄统一使用 opaque pointer。
2. 配置对象统一使用 `create` / `destroy` + `set_*` / `get_*`。
3. 回调对象统一通过只读句柄 + `get_*` 访问；跨线程持有时使用 `retain` / `release`。
4. 错误信息通过 thread-local 的 `librflow_get_last_error()` 获取。
5. ABI 兼容策略是“只增不改”：新增能力优先通过新增函数暴露。
6. 内部 opaque 结构都带 `magic` 字段，用于句柄有效性校验。

## 生命周期

### Client

```text
set_global_config
  -> init
  -> connect
  -> open_stream*
  -> close_stream*
  -> disconnect
  -> uninit
```

### Service

```text
svc_set_global_config
  -> svc_init
  -> svc_connect
  -> [svc_create_stream -> svc_start_stream -> (内部采集 或 svc_push_video_frame*)]
  -> svc_stop_stream
  -> svc_destroy_stream
  -> svc_disconnect
  -> svc_uninit
```

`disconnect` / `uninit` / `destroy_stream` 相关接口都按幂等清理设计。

## 详细设计

- Client：见 [docs/client_api_design.md](./docs/client_api_design.md)
- Service：见 [docs/service_api_design.md](./docs/service_api_design.md)
- 早期评审：见 [docs/client_api_design_review.md](./docs/client_api_design_review.md)

## 当前实现状态

- ABI 层已经基本落齐：`*_create` / `*_destroy` / `*_set_*` / `*_get_*` 等接口均已实现。
- Service 推流链路已打通，需 `RFLOW_SERVICE_ENABLE_WEBRTC_IMPL=ON`。
- Service 侧当前支持两种视频输入模式：
  - SDK 内部采集模式：通过 `librflow_svc_stream_param_set_video_device_path` 或 `librflow_svc_stream_param_set_video_device_index` 配置视频源。
  - 业务侧外部投帧模式：通过 `librflow_svc_push_frame_*` + `librflow_svc_push_video_frame` 向 SDK 投入 I420/NV12 帧。
- `Publisher` 负责桥接 C ABI 与 `PushStreamer` / `SignalingClient`，并根据 stream param 选择内部采集或外部投帧模式。
- `ExternalPushVideoTrackSource` 负责把业务侧投递的 I420/NV12 帧接入 WebRTC 视频编码链路。
- Client 拉流链路在 `RFLOW_CLIENT_ENABLE_WEBRTC_IMPL=ON` 时由 `src/client/impl/rtc_stream/` 提供。

仍为 stub 或未完全收敛的部分：

- `librflow_svc_connect` 目前未接入完整云端鉴权 / license 校验流程。
- `librflow_stream_get_stats` / `librflow_svc_stream_get_stats` 仍返回 `NOT_SUPPORT`。
- Client / Service 两侧信令实现尚未完全合并。
- 线程池仍为内置轻量实现，尚未接第三方 executor。

# libRoboFlow：Client / Service 分层与能力对照

仓库提供两套面向产品的共享库（可选用静态或 ELF `.so`）：

| 产物 | CMake 目标 | 公开头文件入口 | 主要职责 |
|------|-------------|----------------|----------|
| `librflow_client.so` | `rflow_client` | `include/rflow/Client/librflow_client_api.h`、`librflow_common.h` | 设备侧 SDK：连信令、建连、打开拉流会话、回调视频帧与流统计 |
| `librflow_svc.so` | `rflow_svc` | `include/rflow/Service/librflow_service_api.h`、`librflow_common.h` | 设备 / 机器人侧推流服务：建连、推 external / 摄像头、订阅者 Offer、流统计 |

`librflow_common.h` 中的 **Stream stats**、**全局配置**、**日志**、**视频帧句柄** 等为 **两库共享的 C ABI**；实现位于 `src/common`，随 Client 与 Service 一并链入。

## 源码目录与 include 边界

- **`src/common`**：跨 Client/Service 复用的基础类型、日志、媒体辅助（如 `stream_stats`）、ABI 工具。
- **`src/core`**：信令、线程池、runtime knobs（环境变量表）、RTC 域与可选 WebRTC 集成；Client/Service 均可依赖，但 **不得** 在 `src/client` 里 `#include` `service/`，或反向引用。
- **`src/client`**：Client API 实现与（可选）拉流 RTC 实现。
- **`src/service`**：Service API、内部策略与（可选）推/拉 subscriber 等媒体实现。

CMake 上对库目标的 **PRIVATE** include 收窄为：`include`、`src/common`、`src/core`，以及 **`src/client`（含 impl）或 `src/service`（含 impl）**之一，而不是整块 `src`，以减少误用深层头文件。

## 与名称易混文件区分

- **`src/core/runtime/runtime_knobs.*`**：`RFLOW_*` / `WEBRTC_*` 等运行时环境变量的 **集中登记表**（全仓共用）。
- **`src/service/internal/service_default_params.*`**：仅与服务侧默认推流参数（如 `RFLOW_SVC_DEFAULT_FPS` 等）相关的 **薄封装**，从 core `runtime_knobs` 读值；**不要把二者混名**。

## 链接与 ABI 导出

在 Linux/Android 上，共享库链接时使用 **`tools/linkmap/librflow_abi.map`**（GNU `version-script`）：对外仅导出 **`librflow_*` 前缀的 C ABI 符号**，其余符号本地可见，减少对下游的动态符号污染。

**静态库（`RFLOW_BUILD_SHARED=OFF`）**：不经过上述链接脚本，`.a` 中仍包含所有翻译单元符号；下游静态链接时 **仍只应依赖公开 C API（`librflow_*`）**，不要将内部的 C++ 符号当作稳定 ABI。

与本项目相关的 **稳定性约定**：

- **`librflow_stream_stats_*`（retain/release 与各 getter）** 须在 `include/rflow/librflow_common.h` 与 `src/common/media/stream_stats.cpp` 保持一一对应；仓库提供 `tools/check_stream_stats_abi.py`：开启 **`RFLOW_BUILD_TESTS=ON`** 时由 **`ctest`** 运行；开启 **`RFLOW_ENABLE_STRUCTURE_CHECK=ON`** 时与 `tools/check_structure_rules.py` 一并由目标 **`rflow_structure_check`** 运行（适合 CI 单命令跑两类守门）。

## 参考文档

- `docs/client_api_design.md`、`docs/service_api_design.md`：API 能力与状态机细化说明。
- `docs/RUNTIME_KNOBS.md`：运行时开关表（由 `core/runtime/runtime_knobs.cpp` 生成）。

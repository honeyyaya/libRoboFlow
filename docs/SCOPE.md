# 范围冻结（Scope）

本文档冻结 **CommonVideoSDK** 首版实现范围。未在业务侧最终确认前，以文中「默认假设」为准；变更需更新本文件版本与日期。

**v1 架构主文档**：产品拓扑、会话模型、内嵌信令与状态机以 [ARCHITECTURE.md](ARCHITECTURE.md) 及同系列文档为准；本文侧重 **平台、产物命名与集成约束**。若下文历史表述与架构系列冲突，**以架构文档为准**。

## 1. 产品标识

| 项 | 取值 |
|----|------|
| 仓库 / 产品名 | **CommonVideoSDK** |
| 对外 C 符号前缀 | `commonvideo_*`（如 `commonvideo_init`、`commonvideo_pc_create`） |
| 对外 C++（可选） | [`include/commonvideo.hpp`](../include/commonvideo.hpp) 为 C ABI 的薄封装，**Android 与 Linux 宿主均推荐使用纯 C++** 链接平台产物 `.so`（见下） |

### 1.1 动态库文件名（带平台）

发布时 **按平台使用不同文件名**，避免 Linux / Android 产物混用或打包进同一目录时覆盖：

| 平台 | 动态库文件名（推荐） |
|------|----------------------|
| Linux aarch64 | `libcommonvideo_linux.so` |
| Android arm64-v8a | `libcommonvideo_android.so` |

若离线包内需同时存放多 ABI，可在文件名中追加架构，例如 `libcommonvideo_linux_aarch64.so`、`libcommonvideo_android_arm64.so`。CMake `OUTPUT_NAME` 与 Gradle `jniLibs` 按上表配置即可；**对外 C 符号仍为** `commonvideo_*`，与文件名无关。

## 2. 目标平台

| 平台 | ABI / 环境 | 说明 |
|------|------------|------|
| Linux 服务端 | **aarch64**，glibc 环境 | 参考构建与运行基线见下 |
| Android 客户端 | **arm64-v8a**（NDK） | 首版仅 64 位 ARM，不交付 `armeabi-v7a`/`x86_64` |

### 2.1 Linux 参考发行版

- **参考**：Ubuntu **22.04 LTS** aarch64（用于 CI 与本地验证）。
- **兼容策略**：以 **glibc 2.35**（22.04 默认）为最低参考；若需支持更早发行版，需在 `docs/BUILD.md` 中单独声明并做实际机器验证。

### 2.2 Android 级别

| 项 | 首版默认 |
|----|----------|
| **minSdkVersion** | **24**（Android 7.0） |
| **targetSdk** | 与宿主 App 一致（SDK 不强制上限） |
| **NDK** | r26b 或更新（与 `cmake/` 工具链一致） |

若业务要求 minSdk 23 或 26+，在集成前更新本表并回归媒体与权限行为。

## 3. Linux 端媒体能力（v1）

**v1 首版（与 [ARCHITECTURE.md](ARCHITECTURE.md) 一致）**

| 角色 | Linux aarch64 | 说明 |
|------|----------------|------|
| **发布进程** | **发送**：默认 **V4L2 + ALSA** 采集，经 RTC 推流；进程内嵌 **WebSocket JSON 信令 v1**（见 [SIGNALING_V1.md](SIGNALING_V1.md)） | 发布进程使用 SDK **发布能力**，非独立「服务端 SDK 产品线」 |
| **Linux 客户端** | **不支持** | v1 客户端仅为 **Android arm64-v8a 订阅端** |

**历史分阶段表述（仅供参考）**：早期草案曾规划 Linux 以接收为主、发送为 Phase 2；该路线已被 **「Linux 仅发布、Android 仅订阅」** 替代。若代码或旧文档仍提及 Phase 1/2，以实现与架构系列同步结果为准。

**媒体源扩展**：文件 / 网络流等输入为 **Media Source Adapter** 扩展点，见 [MEDIA_SOURCE_ABSTRACTION.md](MEDIA_SOURCE_ABSTRACTION.md)；**不**作为 v1 必选交付。

## 4. Android 端能力（首版）

- **纯 C++（Native）宿主，仅订阅角色**。通过 **NDK** 将本 SDK 编译为 **`libcommonvideo_android.so`**（见 §1.1），业务代码（`.cpp`）直接包含 [`commonvideo.h`](../include/commonvideo.h) 或 [`commonvideo.hpp`](../include/commonvideo.hpp)；**不以 Java/Kotlin/JNI 作为对外主接口**。架构见 [ARCHITECTURE_ANDROID_SUBSCRIBER.md](ARCHITECTURE_ANDROID_SUBSCRIBER.md)。
- 若 App 仍有 Java UI，仅可通过 **极薄 JNI** 将调用转发到自有的 native 层，该 JNI **不属于** 本 SDK 的公开 API 面。
- **解码输出与渲染**：接收路径的解码与缓冲由 SDK 与宿主 native 约定；渲染可使用 **`ANativeWindow`** 等（由宿主实现）。**v1 不以 Android 作为发布采集端**；发布采集在 Linux **V4L2 + ALSA** 侧。集成说明见 [`docs/CPP_INTEGRATION.md`](CPP_INTEGRATION.md)。

## 5. 非目标（Out of Scope）— 首版明确不做

- 跨平台 **x86_64**、**Windows**、**iOS**。
- **Linux 客户端**（v1 无 Linux 订阅 SDK 用法）。
- **多人 / 房间 / 1 对多**（v1 仅 **1 对 1**，见 [SIGNALING_V1.md](SIGNALING_V1.md)）。
- **外置信令服务产品**（v1 信令 **内嵌于 Linux 发布进程**；消息格式见 [SIGNALING_V1.md](SIGNALING_V1.md)）。SDP/ICE 与 PeerConnection 职责边界另见 [SIGNALING.md](SIGNALING.md)。
- **Java/Kotlin 公共 API**。
- 完整 UI 组件库。

## 6. 版本与修订

| 版本 | 日期 | 变更摘要 |
|------|------|----------|
| 0.1 | 2026-04-15 | 初稿：平台、Phase 1/2、minSdk 24、Ubuntu 22.04 aarch64 参考 |
| 0.2 | 2026-04-15 | Android / Linux 对外集成统一为 **纯 C++**（C ABI + 可选 `commonvideo.hpp`） |
| 0.3 | 2026-04-15 | 动态库按平台命名 |
| 1.0 | 2026-04-15 | 品牌与 API：**CommonVideoSDK**，`commonvideo_*`，`libcommonvideo_{linux,android}.so`；仓库目录统一为 **CommonVideoSDK** |
| 1.1 | 2026-04-15 | 与 v1 架构文档对齐：Linux 发布 + 内嵌信令；Android 仅订阅；§5 非目标更新 |

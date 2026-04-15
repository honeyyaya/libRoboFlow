# 范围冻结（Scope）

本文档冻结 **CommonVideoSDK** 首版实现范围。未在业务侧最终确认前，以文中「默认假设」为准；变更需更新本文件版本与日期。

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

## 3. Linux 端媒体能力（发送 / 接收）

业务未二选一时，采用分阶段默认，便于并行推进：

| 阶段 | Linux 能力 | 说明 |
|------|------------|------|
| **Phase 1（首版 SDK 核心）** | **接收** 远端音视频、解码输出（YUV/PCM 或约定缓冲区）、统计与错误回调 | 满足典型「服务端拉流 / 录制 / 分析」 |
| **Phase 2（可选扩展）** | **发送**：自定义视频帧 / 音频帧注入，或对接 V4L2/ALSA 等采集 | 需要单独里程碑与依赖（无头设备差异大） |

**当前冻结**：实现与 API 以 **Phase 1 必选、Phase 2 预留扩展点**（见 `include/commonvideo.h` 中占位与文档）为准。

## 4. Android 端能力（首版）

- **与 Linux 一致：纯 C++ 宿主**。通过 **NDK** 将本 SDK 编译为 **`libcommonvideo_android.so`**（见 §1.1），业务代码（`.cpp`）直接包含 [`commonvideo.h`](../include/commonvideo.h) 或 [`commonvideo.hpp`](../include/commonvideo.hpp)；**不以 Java/Kotlin/JNI 作为对外主接口**。
- 若 App 仍有 Java UI，仅可通过 **极薄 JNI** 将调用转发到自有的 native 层，该 JNI **不属于** 本 SDK 的公开 API 面。
- 采集 / 渲染：**Camera / `ANativeWindow` / MediaCodec（NDK）** 等在平台适配层（C++）实现；与 Linux 共用 Core，仅平台宏与媒体后端不同。集成说明见 [`docs/CPP_INTEGRATION.md`](CPP_INTEGRATION.md)。

## 5. 非目标（Out of Scope）— 首版明确不做

- 跨平台 **x86_64**、**Windows**、**iOS**。
- SDK 内置信令服务器或固定信令协议（仅提供 SDP/ICE 与宿主信令对接边界，见 `docs/SIGNALING.md`）。
- 完整 UI 组件库。

## 6. 版本与修订

| 版本 | 日期 | 变更摘要 |
|------|------|----------|
| 0.1 | 2026-04-15 | 初稿：平台、Phase 1/2、minSdk 24、Ubuntu 22.04 aarch64 参考 |
| 0.2 | 2026-04-15 | Android / Linux 对外集成统一为 **纯 C++**（C ABI + 可选 `commonvideo.hpp`） |
| 0.3 | 2026-04-15 | 动态库按平台命名 |
| 1.0 | 2026-04-15 | 品牌与 API：**CommonVideoSDK**，`commonvideo_*`，`libcommonvideo_{linux,android}.so`；仓库目录统一为 **CommonVideoSDK** |

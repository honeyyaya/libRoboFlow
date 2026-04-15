# 纯 C++ 集成（Android NDK 与 Linux）

**CommonVideoSDK** 在 **Android** 与 **Linux** 上采用 **同一套原生实现**：对外稳定 **C ABI**（[`include/commonvideo.h`](../include/commonvideo.h)），宿主以 **C++** 链接带平台后缀的动态库：**`libcommonvideo_linux.so`**（Linux aarch64）、**`libcommonvideo_android.so`**（Android arm64-v8a），详见 [`SCOPE.md`](SCOPE.md) §1.1。可选使用 [`include/commonvideo.hpp`](../include/commonvideo.hpp) 减少样板代码。

## 1. 原则

| 平台 | 集成方式 |
|------|----------|
| Linux aarch64 | 业务进程（C++）`target_link_libraries(... commonvideo)`，运行时加载 **`libcommonvideo_linux.so`**（同目录或 `LD_LIBRARY_PATH`）。 |
| Android arm64-v8a | **NDK**：CMake `add_library` / `ExternalNativeBuild`，将 SDK 作为 `SHARED` 库链入，产物名为 **`libcommonvideo_android.so`**；业务代码为 `.cpp`，**不**要求 Java/Kotlin 参与核心逻辑。 |

**Java/Kotlin**：不作为 SDK 的公开 API。若产品 UI 为 Java，可自行编写极薄 JNI 转发到自有 native 模块；该层由业务维护，不在本仓库规范内。

## 2. 线程与回调

与 C 头文件注释一致：**回调在 SDK 工作线程**触发，**不是** Android 主线程。纯 C++ 宿主应在回调内做轻量处理，或将事件投递到自有单线程/线程池；避免在回调内长时间阻塞或死锁。

## 3. 构建要点（摘要）

- **C++ 标准**：与 Core 一致（建议 **C++17**）。
- **Android**：`minSdk`、NDK 版本见 [`SCOPE.md`](SCOPE.md)；链接时注意 `-Wl,--exclude-libs,ALL` 等策略与最终 APK 体积由业务决定。
- **Linux**：与参考发行版 glibc 兼容策略见 `SCOPE.md`。

详细 CMake 双工具链与产物布局在后续 `docs/BUILD.md`（若已添加）中维护。

## 4. 与信令

信令传输仍由宿主实现；C++ 侧仅调用 `commonvideo_*` 完成 SDP/ICE 与 PeerConnection 状态机，见 [`SIGNALING.md`](SIGNALING.md)。

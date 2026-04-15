# WebRTC 基座选型

## 1. 选定结论

| 项 | 首版选定 |
|----|----------|
| 基座 | **Google libwebrtc**（与 Chromium 同源） |
| 集成方式 | **源码编译**，为 **Linux aarch64** 与 **Android arm64-v8a** 各产出静态库或合并为 `libcommonvideo_core.a`（再由本仓库链接为 **`libcommonvideo_linux.so`** / **`libcommonvideo_android.so`**） |
| 备选记录 | 商业 SDK / 其他封装若后续引入，需重新评估 **许可、二进制体积、ICE/TURN 行为一致性** |

## 2. 选型理由

- **功能完整**：PeerConnection、SDP、ICE、DTLS-SRTP、Simulcast/SVC（按编译参数）等与浏览器互通路径成熟。
- **双端一致**：同一套实现可减少「Android 能通、Linux 不通」的协议层差异。
- **成本**：构建与升级维护成本高，需在 CI 中固定 **分支/commit** 与 **depot_tools** 流程（见 `docs/CI_AND_SAMPLES.md`）。

## 3. 许可与合规

- libwebrtc 主要采用 **BSD 3-Clause** 等（以官方 `LICENSE` 为准）。
- 发布物需包含：
  - **`NOTICE`** / **第三方许可汇总**（随二进制或 AAR 分发）。
  - 若使用 **openh264** 等可选组件，单独标注专利与许可条款。

具体条文以构建时所检出源码树中的 `LICENSE` 文件为准；本仓库在发布流水线中应 **自动生成** 许可清单（建议后续在 CI 增加脚本）。

## 4. 构建约束（与实现相关）

| 约束 | 说明 |
|------|------|
| 分支固定 | 使用文档化的 **milestone / commit**（例如每季度滚动一次），避免 floating main |
| 符号裁剪 | Linux/Android 均建议 **隐藏非公开符号**，仅导出 `commonvideo_*`（见链接脚本或 visibility） |
| Android | 使用 NDK 与 libwebrtc 的 **Android 构建** 脚本；注意 **16KB page size** 等新政策时重新验证 |
| Linux | 避免静态链接与系统 **OpenSSL/glibc** 冲突；优先使用 libwebrtc 内置 BoringSSL（默认） |

## 5. 与信令的关系

libwebrtc **不包含** 应用层信令。本 SDK 在 C API 层只处理 **SDP / ICE candidate** 的生成与设置；传输由宿主完成（`docs/SIGNALING.md`）。

## 6. 修订记录

| 版本 | 日期 | 变更 |
|------|------|------|
| 0.1 | 2026-04-15 | 初稿：libwebrtc 源码编译为默认基座 |
| 0.2 | 2026-04-15 | 产物与导出符号对齐 **CommonVideoSDK**（`libcommonvideo_*`、`commonvideo_*`） |

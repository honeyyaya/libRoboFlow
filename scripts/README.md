# scripts/

**仅用于"出库"的平台特定构建脚本**。不含 demo 编译、不含联调脚本——后者都在 `apps/`。

| 脚本 | 主机 | 目标 |
|---|---|---|
| `build_linux_arm64.sh` | Linux aarch64 | 出 `librflow_client.so` / `librflow_svc.so`（产物路径 `build/linux-arm64/`，使用 Ninja，`RFLOW_ENABLE_ROCKCHIP_MPP` 默认 OFF） |
| `build_android.ps1` | Windows + NDK | 通过 PowerShell 拉 NDK 工具链 + Qt 出 Android `arm64-v8a` 静态/动态库 |

如果你只想在本机跑通 demo / e2e，直接用 `apps/build.sh`（产物在 `build/`，含 demo + 单测）。

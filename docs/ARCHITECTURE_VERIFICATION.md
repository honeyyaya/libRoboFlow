# 架构文档评审清单（Given / When / Then）

本清单用于评审 **v1 架构文档** 是否自洽、可实施。对应总体说明见 [ARCHITECTURE.md](ARCHITECTURE.md)。

---

## 场景 1：Linux 发布进程进入发布态

| 步骤 | 内容 |
|------|------|
| **Given** | 目标设备为 Linux aarch64，CommonVideoSDK 发布侧已按 [SCOPE.md](SCOPE.md) 集成 |
| **When** | 宿主依次（或等价地）完成 runtime 初始化、默认 **V4L2 + ALSA** `media_source` 创建、`publish_session` 创建，并启动 **内嵌 WebSocket 信令** |
| **Then** | [ARCHITECTURE_PUBLISHER.md](ARCHITECTURE_PUBLISHER.md) 中生命周期与能力均可映射到文档描述；[STATE_MACHINE_AND_ERRORS.md](STATE_MACHINE_AND_ERRORS.md) 中发布会话可从非 `idle` 进入 **`ready`** 或 **`streaming`**（与实现一致）；信令监听与会话标识在 [SIGNALING_V1.md](SIGNALING_V1.md) 中有对应字段或约定 |

**应对章节**：`ARCHITECTURE_PUBLISHER.md` §2–§5；`STATE_MACHINE_AND_ERRORS.md` §2；`ARCHITECTURE.md` §4–§6  

---

## 场景 2：Android 仅订阅、收流

| 步骤 | 内容 |
|------|------|
| **Given** | 目标设备为 Android arm64-v8a，发布端已处于可接受订阅状态 |
| **When** | 宿主创建 runtime 与 `subscribe_session`，连接发布端信令并完成 v1 时序（`auth` → `join` → SDP/ICE） |
| **Then** | [ARCHITECTURE_ANDROID_SUBSCRIBER.md](ARCHITECTURE_ANDROID_SUBSCRIBER.md) 明确 **无** 发布侧能力；[ARCHITECTURE.md](ARCHITECTURE.md) §8 能力矩阵中 Android 仅勾选订阅相关行；解码帧与状态通过 `VideoFrame` / `AudioFrame` / `ConnectionState` 等概念输出 |

**应对章节**：`ARCHITECTURE_ANDROID_SUBSCRIBER.md` §4–§5；`ARCHITECTURE.md` §3、§8  

---

## 场景 3：失败路径可追踪

| 步骤 | 内容 |
|------|------|
| **Given** | 文档定义的异常类别：**鉴权失败**、**会话不存在**、**设备打开失败**、**ICE 超时** |
| **When** | 分别在信令层或媒体层触发上述失败 |
| **Then** | [STATE_MACHINE_AND_ERRORS.md](STATE_MACHINE_AND_ERRORS.md) §4 表格中每一类均有 **目标状态** 与 **`ErrorInfo` 分类**；[SIGNALING_V1.md](SIGNALING_V1.md) §6 中 `error` 语义可覆盖信令侧失败；发布端设备失败落在发布会话 `failed` |

**应对章节**：`STATE_MACHINE_AND_ERRORS.md` §4、§5；`SIGNALING_V1.md` §6  

---

## 场景 4：JNI 不进入公共边界

| 步骤 | 内容 |
|------|------|
| **Given** | 宿主 App 使用 Java/Kotlin UI，但仍使用本 SDK 收流 |
| **When** | 宿主通过自有 JNI 调用 native 中的 `subscribe_session` 等逻辑 |
| **Then** | [ARCHITECTURE_ANDROID_SUBSCRIBER.md](ARCHITECTURE_ANDROID_SUBSCRIBER.md) §2、§7 与 [ARCHITECTURE.md](ARCHITECTURE.md) §1、§9 一致：**无** Java/Kotlin 公共 API 承诺；评审可检查交付物清单中 **仅** `.so` 与 C/C++ 头 |

**应对章节**：`ARCHITECTURE_ANDROID_SUBSCRIBER.md` §2、§7；`ARCHITECTURE.md` §1、§9  

---

## 场景 5：媒体源扩展不破坏订阅语义

| 步骤 | 内容 |
|------|------|
| **Given** | 未来在发布端新增 **文件** 或 **网络流** 作为 `media_source` 实现 |
| **When** | 仅替换或扩展 [MEDIA_SOURCE_ABSTRACTION.md](MEDIA_SOURCE_ABSTRACTION.md) 所述适配层与发布侧配置 |
| **Then** | [ARCHITECTURE_ANDROID_SUBSCRIBER.md](ARCHITECTURE_ANDROID_SUBSCRIBER.md) 与 [SIGNALING_V1.md](SIGNALING_V1.md) **无需**为支持新输入而变更对外语义；[ARCHITECTURE.md](ARCHITECTURE.md) §6 五层图中 **Android 侧不包含** Media Source Adapter |

**应对章节**：`MEDIA_SOURCE_ABSTRACTION.md` §5–§6；`ARCHITECTURE_ANDROID_SUBSCRIBER.md` §5；`ARCHITECTURE.md` §6  

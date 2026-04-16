# 文档索引（CommonVideoSDK / ComVideoSDK v1 架构）

## 架构文档阅读顺序（v1）

1. [总体架构](ARCHITECTURE.md) — 产品边界、拓扑、五层模块、非目标、公开概念索引  
2. [信令协议 v1](SIGNALING_V1.md) — WebSocket JSON、消息与时序  
3. [状态机与错误模型](STATE_MACHINE_AND_ERRORS.md) — 会话与连接状态、失败路径、回调约束  
4. [发布端架构（Linux aarch64）](ARCHITECTURE_PUBLISHER.md)  
5. [Android 订阅端架构（arm64-v8a）](ARCHITECTURE_ANDROID_SUBSCRIBER.md)  
6. [媒体源抽象](MEDIA_SOURCE_ABSTRACTION.md)  

## 其他文档

| 文档 | 说明 |
|------|------|
| [范围冻结](SCOPE.md) | 平台、产物命名、与 v1 架构对齐后的能力边界 |
| [信令边界（通用）](SIGNALING.md) | 与 libwebrtc 相关的 SDP/ICE 职责；v1 内嵌信令见 SIGNALING_V1 |
| [WebRTC 基座](WEBRTC_BASE.md) | libwebrtc 选型与构建约束 |
| [C/C++ 集成](CPP_INTEGRATION.md) | 宿主链接与集成入口，详细行为以架构文档为准 |
| [架构文档评审清单](ARCHITECTURE_VERIFICATION.md) | Given/When/Then 验收项 |

## 与历史文档的关系

v1 **以本目录下架构文档系列为准**。若 [SCOPE.md](SCOPE.md) 或 [SIGNALING.md](SIGNALING.md) 中仍有「宿主全权转发信令」等表述，仅适用于**未使用内嵌信令主机**的集成形态；v1 默认形态见 [ARCHITECTURE.md](ARCHITECTURE.md) 与 [SIGNALING_V1.md](SIGNALING_V1.md)。

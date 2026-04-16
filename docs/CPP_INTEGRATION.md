# C/C++ 集成说明

本文档描述宿主如何以 **C ABI**（及可选 C++ 薄封装）集成本 SDK。**行为边界、会话模型、信令与状态机**以架构文档为准，本文不重复展开。

## 文档入口

- 索引与阅读顺序：[docs/README.md](README.md)  
- 总体架构：[ARCHITECTURE.md](ARCHITECTURE.md)  
- Linux 发布进程：[ARCHITECTURE_PUBLISHER.md](ARCHITECTURE_PUBLISHER.md)  
- Android 订阅端：[ARCHITECTURE_ANDROID_SUBSCRIBER.md](ARCHITECTURE_ANDROID_SUBSCRIBER.md)  

## 产物与符号

- 动态库命名、平台矩阵见 [SCOPE.md](SCOPE.md) §1、§2。  
- 对外 C 符号前缀与头文件路径以仓库 `include/` 为准（当前文档化前缀为 `commonvideo_*`）。  

## 宿主职责（概要）

- **Linux 发布进程**：初始化 runtime、按需启动内嵌 WebSocket 信令、创建媒体源与发布会话；鉴权通过 SDK 提供的验证回调完成（协议字段见 [SIGNALING_V1.md](SIGNALING_V1.md)）。  
- **Android 订阅端**：初始化 runtime、创建订阅会话、连接发布端信令地址并完成 v1 信令时序；在回调中接收解码后的音视频帧与统计。  
- **Java/Kotlin UI**：仅可通过宿主自有 JNI 转发到 native；JNI **不属于** SDK 公共 API（见 [ARCHITECTURE_ANDROID_SUBSCRIBER.md](ARCHITECTURE_ANDROID_SUBSCRIBER.md)）。  

## 信令与 WebRTC

- 应用层信令消息格式：**v1 固定为** [SIGNALING_V1.md](SIGNALING_V1.md)。  
- SDP/ICE 与 PeerConnection 语义：另见 [SIGNALING.md](SIGNALING.md)、[WEBRTC_BASE.md](WEBRTC_BASE.md)。  

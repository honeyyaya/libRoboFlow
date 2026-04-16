# Android 订阅端架构（arm64-v8a）

## 1. 范围

本文档描述 **Android arm64-v8a** 客户端如何使用 CommonVideoSDK 的 **订阅能力**：**runtime**、**subscribe_session**、连接 **发布进程内嵌信令**、完成 WebRTC 建连并接收 **解码后的音视频帧** 与状态/统计。总体关系见 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 2. 公开面：C ABI + 可选 C++

- **公共 API**：以 **C ABI** 为准；可选 **C++ 薄封装** 与头文件一并提供。  
- **Java / Kotlin**：**不属于** SDK 公共边界。若 App 使用 Java/Kotlin UI，宿主通过 **自有极薄 JNI** 将调用转入 native；该 JNI 层 **不得** 冒充或替代官方公开 API。  

## 3. 生命周期（推荐顺序）

1. **创建 runtime**：`GlobalConfig` 等进程/应用级初始化（具体粒度以实现为准）。  
2. **创建 subscribe_session**：传入 `SubscribeConfig`（含发布端信令 **WebSocket URL**、STUN/TURN、会话标识等）。  
3. **连接信令**：与发布端 **Embedded Signaling Host** 建立 WebSocket；按 [SIGNALING_V1.md](SIGNALING_V1.md) 发送 **`auth`**、**`join`**，交换 **`offer` / `answer`**、**`ice`**。  
4. **运行中**：通过 **callbacks** 接收 `ConnectionState`、`VideoFrame`、`AudioFrame`、`StatsSnapshot`；错误通过 `ErrorInfo` 上报。  
5. **关闭**：关闭 `subscribe_session`，必要时销毁 runtime。  

## 4. 能力清单

| 能力 | 说明 |
|------|------|
| 初始化 / 销毁 runtime | 与宿主 Application 生命周期对齐 |
| 创建 / 关闭订阅会话 | 一对一订阅；与单个发布方配对 |
| 连接发布端信令地址 | WebSocket URL；TLS 策略由部署与宿主配置决定 |
| 完成 auth、join、offer/answer、ice | 客户端侧信令行为由 Session Layer 驱动，载荷格式见 SIGNALING_V1 |
| 输出解码后音视频帧、状态、统计 | `VideoFrame` / `AudioFrame` / `ConnectionState` / `StatsSnapshot` |

## 5. 与发布端的角色边界

- Android 端 **仅承担订阅角色**：不启动内嵌信令服务器，不创建 `publish_session`，不暴露发布侧 `media_source` 能力。  
- 若未来 Linux 发布侧扩展输入类型，**不得**要求修改 Android 订阅侧对外语义（见 [MEDIA_SOURCE_ABSTRACTION.md](MEDIA_SOURCE_ABSTRACTION.md)）。  

## 6. 公开类型语义（文档级）

下列类型在架构上必须可区分职责（字段以实现为准）：

| 类型 | 订阅端语义 |
|------|------------|
| `GlobalConfig` | 日志、全局 RTC 参数等 |
| `SubscribeConfig` | 信令 URL、ICE、会话 id、超时等 |
| `VideoFrame` | 解码后视频；缓冲区所有权与线程约束见 [STATE_MACHINE_AND_ERRORS.md](STATE_MACHINE_AND_ERRORS.md) |
| `AudioFrame` | 解码后音频 |
| `StatsSnapshot` | 接收路径统计 |
| `ErrorInfo` | 信令失败、RTC 失败、超时等 |
| `ConnectionState` | 信令连接与媒体连接聚合状态 |

## 7. JNI 与宿主分层（公共边界）

- **SDK 交付物**：`.so` + C/C++ 头文件（及可选 C++ wrapper）。  
- **宿主 Java 层**：仅调用 **宿主自写** 的 JNI；JNI 实现位于宿主工程，**不在** SDK 公共 API 列表中。  
- 评审检查：任何「必须通过 SDK 提供的 Java 类」的集成方式，均视为 **偏离 v1 架构**。  

## 8. 相关文档

- [ARCHITECTURE.md](ARCHITECTURE.md)  
- [SIGNALING_V1.md](SIGNALING_V1.md)  
- [STATE_MACHINE_AND_ERRORS.md](STATE_MACHINE_AND_ERRORS.md)  
- [CPP_INTEGRATION.md](CPP_INTEGRATION.md)  

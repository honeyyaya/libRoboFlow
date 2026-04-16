# 媒体源抽象（v1）

## 1. 目的

将 **发布侧媒体输入** 与 **Session Layer / RTC Core** 解耦，使 v1 默认使用 **设备采集**，并在不修改 **Android 订阅侧接口语义** 的前提下，预留 **文件流、网络流** 等替换实现。

## 2. 概念：`media_source`

- **职责**：向 `publish_session` 提供 **有时序的音频/视频原始或已协商格式** 的数据通道（精确格式由实现与 `PublishConfig` 约定）。  
- **生命周期**：通常短于 runtime，长于单次 WebSocket 连接；与 `publish_session` 绑定关系见 [ARCHITECTURE_PUBLISHER.md](ARCHITECTURE_PUBLISHER.md)。  

## 3. 与相邻层的边界

| 相邻层 | 边界说明 |
|--------|----------|
| **Session Layer** | 创建/启动/停止采集；处理「无帧」「设备热插拔」等策略性决策 |
| **RTC Core** | 接收符合约定的帧或 track；编码、发送、码控在 RTC Core |
| **Embedded Signaling** | **不**直接感知 `media_source` |

## 4. v1 默认实现：V4L2 + ALSA

| 模块 | 责任（架构级） |
|------|----------------|
| **V4L2** | 打开设备、设置格式、采集缓冲、时间戳来源、错误上报 |
| **ALSA** | 打开 PCM、参数协商、读帧/回调、与视频时间戳对齐策略（若需要） |

**错误**：设备不存在、权限、格式不支持等 → 通过 **`ErrorInfo`** 与发布会话状态 **`failed`** 上报，见 [STATE_MACHINE_AND_ERRORS.md](STATE_MACHINE_AND_ERRORS.md)。

## 5. 非 v1 范围（仅扩展点）

以下输入类型 **不在 v1 产品承诺** 内，仅作为适配层演进方向：

- **文件**（本地录制文件、裸流文件等）  
- **网络流**（RTSP、HTTP 等）  

引入时须 **只扩展 Media Source Adapter** 及发布侧配置：**不得**改变 Android 端 `subscribe_session` 对 **解码帧、状态、统计** 的语义。

## 6. 扩展原则

- 新源类型实现 **相同抽象接口**（概念上）：`start` / `stop` / `reconfigure` / 错误回调。  
- **会话与信令协议 v1** 不随新源类型增加消息类型（除非另开协议版本）。  

## 7. 相关文档

- [ARCHITECTURE.md](ARCHITECTURE.md)  
- [ARCHITECTURE_PUBLISHER.md](ARCHITECTURE_PUBLISHER.md)  
- [ARCHITECTURE_ANDROID_SUBSCRIBER.md](ARCHITECTURE_ANDROID_SUBSCRIBER.md)  

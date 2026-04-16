# 发布端架构（Linux aarch64）

## 1. 范围

本文档描述 **Linux aarch64 发布进程** 如何使用 CommonVideoSDK：**runtime**、**内嵌信令**、**media_source**、**publish_session** 及 **鉴权回调**。与总体架构的对应关系见 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 2. 生命周期（推荐顺序）

1. **创建 runtime**：加载 `GlobalConfig`（或等价初始化参数），完成进程级 RTC 与日志等初始化。  
2. **（可选）启动内嵌信令**：绑定监听地址/端口，使 **Embedded Signaling Host** 开始接受 WebSocket 连接。  
3. **创建 media_source**：建立默认 **V4L2 + ALSA** 采集路径（或后续扩展源类型，见 [MEDIA_SOURCE_ABSTRACTION.md](MEDIA_SOURCE_ABSTRACTION.md)）。  
4. **创建 publish_session**：绑定 `PublishConfig`、关联 `media_source`；会话层进入「可接受订阅方建连」状态。  
5. **运行中**：对每条订阅连接执行 [SIGNALING_V1.md](SIGNALING_V1.md) 时序；SDK 通过 **认证回调** 校验 `auth` 消息。  
6. **关闭**：关闭 `publish_session` → 释放 `media_source` → 停止内嵌信令 → 销毁 runtime。  

实际 API 顺序以实现为准；上表用于架构评审时检查依赖是否完备。

## 3. 能力清单

| 能力 | 说明 |
|------|------|
| 初始化 / 销毁 runtime | 单进程通常一次；多实例策略以实现为准 |
| 启动 / 停止内嵌信令 | 控制 WebSocket 服务生命周期；与 `PublishConfig` 中监听参数对应 |
| 创建设备采集源 | v1 默认 **V4L2（视频）+ ALSA（音频）** |
| 创建 / 关闭发布会话 | 一对一发布；与单个远端订阅方配对 |
| 注入鉴权验证回调 | 收到 `auth`（含 `token`）时调用宿主逻辑；通过/拒绝映射到信令与状态机，见 [STATE_MACHINE_AND_ERRORS.md](STATE_MACHINE_AND_ERRORS.md) |

## 4. 默认媒体输入

- **视频**：V4L2（设备打开、格式协商、错误上报的责任边界见 [MEDIA_SOURCE_ABSTRACTION.md](MEDIA_SOURCE_ABSTRACTION.md)）。  
- **音频**：ALSA。  
- **非 v1 范围**：文件、网络拉流等仅作为适配层扩展点预留，不改变 [ARCHITECTURE_ANDROID_SUBSCRIBER.md](ARCHITECTURE_ANDROID_SUBSCRIBER.md) 语义。  

## 5. 内嵌信令与 Session 的协作

- **Embedded Signaling Host** 解析 JSON、维护与 **Android 客户端** 的 WebSocket 连接，并将 `auth` / `join` / SDP / ICE 等事件交给 **Session Layer**。  
- **Session Layer** 驱动 **RTC Core** 生成 answer、添加 ICE，并将 outbound 消息交回信令主机发送。  
- 消息集、字段与时序以 [SIGNALING_V1.md](SIGNALING_V1.md) 为准。  

## 6. 鉴权（架构边界）

- 协议层：见 `auth` 消息中的 **`token`** 字段（及必要关联字段如会话 id，以 SIGNALING_V1 为准）。  
- 实现层：发布进程通过 SDK 暴露的 **验证回调** 将 token（及上下文）交给宿主；宿主返回允许/拒绝。  
- **本文档不定义** token 格式、签名算法、过期策略。  

## 7. 公开类型（发布端主要使用）

- `GlobalConfig`、`PublishConfig`、`ErrorInfo`、`ConnectionState`、`StatsSnapshot`（若发布端对外暴露统计）。  
- 输出给宿主的 **解码帧类型** 非 v1 发布端文档承诺重点；v1 聚焦 **上行发布**。  

## 8. 相关文档

- [ARCHITECTURE.md](ARCHITECTURE.md)  
- [SIGNALING_V1.md](SIGNALING_V1.md)  
- [STATE_MACHINE_AND_ERRORS.md](STATE_MACHINE_AND_ERRORS.md)  
- [MEDIA_SOURCE_ABSTRACTION.md](MEDIA_SOURCE_ABSTRACTION.md)  

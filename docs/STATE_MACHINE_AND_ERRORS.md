# 状态机与错误模型（v1）

## 1. 视角

v1 从三条线描述行为，彼此耦合但通过 **`ConnectionState`**、**`ErrorInfo`** 与回调统一对外：

1. **发布会话（Linux）**：`publish_session` 从创建到关闭。  
2. **订阅会话（Android）**：`subscribe_session` 从创建到关闭。  
3. **信令与媒体连接**：WebSocket 与 PeerConnection 的聚合状态（具体粒度以实现为准）。  

## 2. 发布会话状态（概念）

| 状态 | 含义 |
|------|------|
| `idle` | 未创建或已销毁 |
| `initializing` | 创建中，绑定配置与 `media_source` |
| `ready` | 可接受订阅；内嵌信令可处理 `join` |
| `streaming` | 至少一路订阅建连成功，媒体发送中 |
| `stopping` | 正在关闭轨道与 PC |
| `failed` | 不可恢复或宿主选择终止；携带 `ErrorInfo` |

**设备打开失败**（V4L2/ALSA）：从 `initializing` 转入 **`failed`**，错误码归类为 **设备/资源**；见 §4。

## 3. 订阅会话状态（概念）

| 状态 | 含义 |
|------|------|
| `idle` | 未创建或已销毁 |
| `connecting_signaling` | WebSocket 连接与 `auth`/`join` 进行中 |
| `negotiating` | SDP/ICE 交换中 |
| `connected` | 媒体路径已建立 |
| `reconnecting` | （可选）信令或 ICE 重试中；若 v1 不实现，可合并入 `connecting_signaling` |
| `closing` | 主动关闭 |
| `failed` | 失败终态 |

## 4. 失败路径与错误落点

下列场景须在实现与文档中 **有明确状态迁移** 与 **`ErrorInfo.code` 分类**（具体数值以实现为准）：

| 场景 | 期望落点 |
|------|----------|
| **鉴权失败** | 订阅端：`connecting_signaling` → `failed`；发布端：拒绝连接或发送 `error`；信令可断开 |
| **会话不存在 / join 拒绝** | 订阅端：`connecting_signaling` → `failed`；信令 `error` |
| **设备打开失败（发布端）** | 发布端：`initializing` 或 `ready` → `failed`；上报 `ErrorInfo` |
| **ICE 超时 / 连通性失败** | 相关会话 → `failed` 或经 `reconnecting` 后 `failed`；回调 `ErrorInfo` |
| **SDP 设置失败** | `negotiating` → `failed` |
| **对端 `close` 或 WebSocket 断开** | 转入 `closing` / `idle`；是否视为错误由 `ErrorInfo` 标志区分 |

## 5. `ErrorInfo`（文档级）

- **`code`**：机器可读；与 [SIGNALING_V1.md](SIGNALING_V1.md) 中 `error.code` 可建立映射表。  
- **`message`**：人类可读。  
- **可恢复性**（建议）：字段或码段区分 **可重试** vs **须重建会话**；v1 至少应能区分 **鉴权/会话配置错误** 与 **网络/ICE 临时失败**。  

## 6. `ConnectionState`（文档级）

- 对外暴露 **聚合状态**，可映射内部子状态（信令已连 / ICE 已连 / DTLS 就绪等）。  
- **与回调顺序**：状态变更回调应先于或原子于依赖该状态的帧回调（具体顺序以实现文档固定）。  

## 7. 回调线程约束（架构约定）

下列约束供实现与宿主集成遵循，避免死锁与数据竞争：

- **禁止**在 SDK 回调内执行 **长时间阻塞**（磁盘 IO、未设限的同步信令等待等）。  
- **重入**：不假设回调期间可重入同一 API；宿主应在回调内 **投递** 到自有队列再调用 SDK。  
- **`VideoFrame` / `AudioFrame` 缓冲区**：在 **明确文档化的生命周期** 内有效；若需跨线程延长使用，宿主须 **拷贝** 或调用 SDK 提供的 **retain/release**（若实现提供）。未文档则以上一版实现为准。  
- **统计回调**（`StatsSnapshot`）：频率可能较高，宿主应节流展示层。  

## 8. 相关文档

- [SIGNALING_V1.md](SIGNALING_V1.md)  
- [ARCHITECTURE_PUBLISHER.md](ARCHITECTURE_PUBLISHER.md)  
- [ARCHITECTURE_ANDROID_SUBSCRIBER.md](ARCHITECTURE_ANDROID_SUBSCRIBER.md)  

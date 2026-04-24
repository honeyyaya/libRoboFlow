# libRoboFlow Client 侧图（新人 / 评审用）

本目录用 **Mermaid** 描述 `librflow_client` 的时序、架构与状态；在支持 Mermaid 的渲染环境（如 GitHub、VS Code 预览、部分 Wiki）中可直接查看。

| 文件 | 内容 |
|------|------|
| [01-sequences.md](./01-sequences.md) | 时序图 3 张：建连、开流、收视频帧（API 一点下去发生什么） |
| [02-c4-container.md](./02-c4-container.md) | C4 容器/组件 + 虚线未来态（core/signal 等） |
| [03-state-machines.md](./03-state-machines.md) | 生命周期 + 流状态 + 与幂等 API 的约束 |

> 与代码路径对应时以 `src/client/` 与 `include/rflow/Client/librflow_client_api.h` 为准；`RFLOW_CLIENT_ENABLE_WEBRTC_IMPL` 未开启时无真实 WebRTC 与 `impl/webrtc` 拉流，行为以 stub 为准。

# C4：Client 拉流（容器/组件 + 未来态）

阅读约定：**实线**＝当前依赖/调用方向；**虚线**＝计划中的抽象或替代关系。

> 用 flowchart 表达 C4「容器/上下文」；若你本地有 [Mermaid C4 扩展](https://mermaid.js.org/syntax/c4.html)，可自行迁移为 `C4Context` / `C4Container` 图。

---

## 容器级（C4 语境：同进程 + 外部系统）

```mermaid
flowchart TB
    subgraph 业务进程
        App[业务/测试]
    end

    subgraph libRoboFlow["librflow_client / core（同进程）"]
        CAPI["C API · lifecycle stream handles"]
        ST["State · lifecycle streams config"]
        Mgr["WebRtcPullManager"]
        WPS["WebRtcPullStream"]
        SC["SignalingClient per-stream"]
        RTC["rflow::rtc PCFactory"]
        TH["rflow::thread 池"]
        SIGSTUB["rflow::signal stub init/shutdown"]
    end

    SigSrv((信令服务端))
    Remote((对端 WebRTC))

    App -->|librflow_*| CAPI
    CAPI --> ST
    CAPI --> Mgr
    Mgr --> WPS
    WPS --> SC
    WPS --> RTC
    CAPI -->|init 顺序| TH
    CAPI --> SIGSTUB
    SC -->|TCP+JSON| SigSrv
    WPS -->|ICE/媒体| Remote

    SIGFUTURE["core::signal 未来 长连 鉴权 多路复用"]
    SC -.->|未来替代或收编| SIGFUTURE
    TH -.->|未来：统一回调派发| CAPI
```

---

## 组件级（C4 Component，Client 子域）

```mermaid
flowchart LR
    subgraph 对外
        H[librflow_client_api.h]
    end

    subgraph 编排
        L[lifecycle]
        S[stream]
        I[infrastructure]
    end

    subgraph 实现层
        M[WebRtcPullManager]
        P[WebRtcPullStream]
        G[SignalingClient]
        F[webrtc_frame_converter]
    end

    subgraph core
        R[rtc: peer_connection_factory 等]
        T[thread 池]
        FS[signal stub]
    end

    H --> L
    H --> S
    L --> I
    S --> I
    S --> M
    I --> R
    I --> T
    I --> FS
    M --> P
    P --> G
    P --> R
    P --> F
    F -->|librflow_video_frame_*| H
```

### 虚线：后续扩展落点

| 方向 | 说明 |
|------|------|
| `core::signal` 实装 | 设备级/会话级信令长连；`send_notice` / `service_req` 走统一通道；可与「单连接多路 index」结合，减少 per-stream `SignalingClient`。 |
| `rflow::thread` | 所有 `on_*` 经固定工作线程/池派发，满足 `librflow_common` 中线程模型承诺。 |
| 配置全量下推 | `signal_config` 的 url / 超时 / 重连 / ICE(STUN/TURN) 自 global_config 注入，替代管理类内硬编码默认项。 |

# 时序图：建连、开流、收帧

说明：虚线部分表示「异步/在其它线程上」的示意；实际回调线程以当前实现与后续「统一回调线程池」演进而定。

---

## 1. 建连（`librflow_init` → `librflow_connect`）

```mermaid
sequenceDiagram
    autonumber
    participant App as 业务/测试代码
    participant API as C API lifecycle.cpp
    participant St as rflow::client::State
    participant Inf as infrastructure init/on_connect
    participant Th as rflow::thread
    participant Rtc as rflow::rtc
    participant Sig as rflow::signal stub
    participant Mgr as WebRtcPullManager

    Note over App: 可选：librflow_set_global_config(含 signal url)
    App->>API: librflow_init()
    API->>St: lock
    API->>Inf: init_infrastructure()
    Inf->>Th: initialize()
    Inf->>Rtc: initialize() → PeerConnectionFactory 等
    Inf->>Sig: initialize()
    Inf-->>API: RFLOW_OK
    API->>St: lifecycle = kInited, unlock
    API-->>App: RFLOW_OK

    App->>API: librflow_connect(connect_info, connect_cb)
    API->>St: lock, 校验, 保存 connect, kConnecting
    API->>St: 取 global_config.signal.url、device_id
    API->>St: unlock
    API->>Inf: on_connect_succeeded(url, device_id)
    Inf->>Mgr: Init(url, device_id) [仅 WebRTC 构建]
    Inf-->>API: err
    API->>St: lock, lifecycle = kConnected 或 回退
    API->>St: unlock
    App-->>App: 锁外 connect_cb.on_state(CONNECTED)
```

> `librflow_connect` 当前语义：**不经过独立设备鉴权长连**，主要是拉起基础设施与 `WebRtcPullManager` 配置；真实设备/会话信令在「未来 core/signal」中扩展。

---

## 2. 开流（`librflow_open_stream` → 信令 + 建 PC）

```mermaid
sequenceDiagram
    autonumber
    participant App
    participant API as C API stream.cpp
    participant St as State::streams
    participant Mgr as WebRtcPullManager
    participant Pull as WebRtcPullStream
    participant SigC as SignalingClient per-stream
    participant Srv as 信令服务端

    App->>API: librflow_open_stream(index, param, stream_cb, &handle)
    API->>St: 校验 kConnected, index 去重, emplace stream_s
    API->>Mgr: OpenStream(index, param, state_sink, frame_sink, &pull)
    Mgr->>Pull: make_shared(…, factory, url, device_id)
    Mgr->>Pull: SetStateSink / SetFrameSink
    Mgr->>Pull: Start()
    Pull->>SigC: Start() TCP connect + register JSON
    SigC->>Srv: 行式 JSON
    Note over Srv,Pull: 等待对端/服务端下发 offer
    Srv-->>SigC: offer
    SigC-->>Pull: on_offer
    Pull->>Pull: CreatePC, SetRemote, CreateAnswer, SetLocal, SendAnswer
    Pull->>SigC: answer / ice
    Note over Pull: ICE 与轨道就绪后
    Pull-->>App: state_sink(OPENED) → on_state
```

> `open_stream` 失败路径：`Manager::OpenStream` 失败会从 `State.streams` 移除该次登记并返回错误码，不会泄漏 handle。

---

## 3. 收帧（`OnTrack` → `on_video`）

```mermaid
sequenceDiagram
    autonumber
    participant W as libwebrtc PC Track
    participant Obs as PeerConnectionObserver
    participant FA as FrameAdapter VideoSink
    participant Sink as frame_sink 闭包 stream.cpp
    participant Conv as webrtc_frame_converter
    participant App

    W->>Obs: OnTrack(video transceiver)
    Obs->>W: track->AddOrUpdateSink(FrameAdapter)
    W->>FA: OnFrame(webrtc::VideoFrame)
    FA->>Sink: frame_sink(frame)
    Sink->>Conv: MakeVideoFrameFromWebrtc, I420 buffer
    Conv-->>Sink: librflow_video_frame_t refcount=1
    Sink->>App: stream_cb.on_video(handle, frame, userdata)
    Sink->>Conv: librflow_video_frame_release(frame)
```

> 若需跨栈持有帧，应使用 `librflow_video_frame_retain`，用完 `release`（与 `librflow_common.h` 说明一致）。

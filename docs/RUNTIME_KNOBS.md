# libRoboFlow Runtime Knobs

本表由 `core/runtime/runtime_knobs.cpp` 中 `kKnobTable` 自动生成（46 项）。


## signal

| name | kind | default | range | alias | doc |
|------|------|---------|-------|-------|-----|
| `RFLOW_VERBOSE_SIGNAL` | bool | 0 | - | - | 信令客户端冗长日志（register_json 等） |
| `RFLOW_SIGNALING_TIMING_TRACE` | bool | 0 | - | - | 信令时序追踪（offer→answer→ICE 各阶段耗时） |

## rtc_factory

| name | kind | default | range | alias | doc |
|------|------|---------|-------|-------|-----|
| `RFLOW_ZERO_PLAYOUT_MIN_PACING_MS` | int | 2 | [0,20] | - | JitterBuffer playout 最小 pacing；0=极致低时延 |
| `RFLOW_MAX_DECODE_QUEUE_SIZE` | int | 3 | [2,16] | - | 解码队列上限；越小越低时延但更易丢帧 |
| `RFLOW_ENABLE_DECODE_QUEUE_GUARD` | bool | 0 | - | - | 启用解码队列的 guard 自动丢帧 |
| `RFLOW_DECODE_QUEUE_GUARD_CAP` | int | 6 | [4,12] | - | decode queue guard 容量 |
| `RFLOW_ENABLE_FLEXFEC` | bool | 1 | - | - | FlexFEC（默认开；设为 0/n/f 关闭） |
| `RFLOW_FIELD_TRIALS_APPEND` | string | "" | - | - | RTC FieldTrials 字符串附加项（实验功能开关） |
| `RFLOW_SYNC_GETSTATS_TIMEOUT_MS` | int | 1500 | [200,60000] | - | 同步阻塞等待 PeerConnection GetStats 回调超时(ms)，用于拉流/推流 QoS CollectStats |

## media.trace

| name | kind | default | range | alias | doc |
|------|------|---------|-------|-------|-----|
| `RFLOW_LATENCY_TRACE` | bool | 0 | - | - | 推流端到端延迟单段追踪（capture/encode/send 等） |
| `RFLOW_E2E_LATENCY_TRACE` | bool | 0 | - | - | 推/拉两端拼接的 E2E latency trace（trace_id 关联） |
| `RFLOW_MEDIA_TIMING_TRACE` | bool | 0 | - | - | MPP 编/解码器内部 timing 追踪 |
| `RFLOW_MEDIA_TIMING_TRACE_EVERY_N` | int | 60 | [1,600] | - | MEDIA_TIMING_TRACE 采样步长（每 N 帧一行） |
| `RFLOW_MJPEG_TO_H264_TRACE` | bool | 0 | - | - | MJPEG→H264 路径专用 trace |
| `RFLOW_MJPEG_DEC_TRACE` | bool | 0 | - | - | MJPEG 解码器内部追踪 |

## media.capture

| name | kind | default | range | alias | doc |
|------|------|---------|-------|-------|-----|
| `RFLOW_MEDIA_THREAD_SCHED` | string | "" | - | - | 媒体线程调度策略：rr / fifo / nice |
| `RFLOW_MEDIA_THREAD_RR_PRIO` | int | 20 | [1,90] | - | SCHED_RR 优先级 |
| `RFLOW_MEDIA_THREAD_NICE` | int | -8 | [-20,19] | - | nice 值（仅当 SCHED=nice） |
| `RFLOW_MJPEG_DECODE_CPU` | string | "" | - | - | MJPEG 解码线程 CPU 亲和（taskset 风格 mask 或单 CPU id） |
| `RFLOW_V4L2_CAPTURE_CPU` | string | "" | - | - | V4L2 capture 线程 CPU 亲和 |

## media.mjpeg

| name | kind | default | range | alias | doc |
|------|------|---------|-------|-------|-----|
| `RFLOW_MJPEG_DECODE_QUEUE_MAX_WAIT_MS` | int | 25 | [0,5000] | - | MJPEG 解码队列最大等待毫秒；超时丢帧 |
| `RFLOW_MJPEG_ZERO_COPY_TO_ENC` | bool | 0 | - | - | MJPEG → MPP H264 编码零拷贝路径开关 |
| `RFLOW_MJPEG_DECODE_INLINE` | bool | 0 | - | - | MJPEG 解码内联在 capture 线程（省一次队列+线程切换） |
| `RFLOW_MJPEG_DEC_LOW_LATENCY` | bool | 0 | - | - | MJPEG 解码器低延迟模式（同步、关池化） |
| `RFLOW_MJPEG_RGA_MAX_ASPECT` | int | 4 | [1,32] | - | 允许 RGA 处理的最大宽高比（避免极端拉伸） |

## media.pipeline

| name | kind | default | range | alias | doc |
|------|------|---------|-------|-------|-----|
| `RFLOW_PUSH_OUTBOUND_STATS_INTERVAL_SEC` | int | 0 | [0,60] | - | 推流端 outbound RTC stats 周期日志间隔（秒；0=关闭） |
| `RFLOW_SKIP_LOOPBACK_RECV` | bool | 0 | - | - | 压测：推流端跳过 loopback recv，节省回环 CPU |

## media.dump

| name | kind | default | range | alias | doc |
|------|------|---------|-------|-------|-----|
| `RFLOW_DUMP_OFFER` | bool | 0 | - | - | 推流端 dump 本地 Offer SDP 到 stderr |
| `RFLOW_DUMP_LOCAL_ANSWER` | bool | 0 | - | - | 拉流端 dump 本地 Answer SDP 到 stderr |
| `RFLOW_DUMP_REMOTE_OFFER` | bool | 0 | - | - | 拉流端 dump 远端 Offer SDP 到 stderr |

## mpp.encoder

| name | kind | default | range | alias | doc |
|------|------|---------|-------|-------|-----|
| `RFLOW_MPP_ENC_HOR_STRIDE_ALIGN` | int | 16 | [1,256] | - | MPP 编码水平 stride 对齐 |
| `RFLOW_MPP_ENC_INTRA_REFRESH_MODE` | int | 0 | [0,3] | - | MPP intra refresh 模式（0=off） |
| `RFLOW_MPP_ENC_INTRA_REFRESH_ARG` | int | 0 | [1,512] | - | MPP intra refresh 参数 |
| `RFLOW_MPP_ENC_SPLIT_BYTES` | int | 0 | [0,4096] | - | MPP 编码切片字节数（slice splitting） |
| `RFLOW_MPP_ENC_IDR_MIN_INTERVAL_MS` | int | 800 | [0,5000] | - | 强制 IDR 之间最小间隔毫秒 |
| `RFLOW_MPP_ENC_IDR_LOSS_QUICK_MS` | int | 180 | [0,2000] | - | 丢包后快速触发 IDR 的窗口毫秒 |
| `RFLOW_MPP_ENC_IDR_FORCE_MAX_WAIT_MS` | int | 3000 | [200,15000] | - | 强制 IDR 命令最大等待毫秒 |
| `RFLOW_MPP_ENC_DEBUG` | bool | 0 | - | - | MPP 编码 debug 日志 |
| `RFLOW_MPP_ENC_USE_SYNC` | bool | 0 | - | - | MPP 编码同步模式 |
| `RFLOW_MPP_ENC_USE_TASK` | bool | 0 | - | - | MPP 编码 task 模式 |
| `RFLOW_MPP_ENC_TASK_READ_PACKET` | bool | 1 | - | - | task 模式下主动 read packet |
| `RFLOW_MPP_ENC_NATIVE_ZERO_COPY` | bool | 1 | - | - | MPP 编码原生零拷贝路径 |
| `RFLOW_MPP_ENC_NATIVE_ZERO_COPY_STRICT` | bool | 0 | - | - | 零拷贝严格模式（失败不回退） |
| `RFLOW_MPP_ENC_NATIVE_ZERO_COPY_FAILS` | int | 3 | [1,50] | - | 零拷贝连续失败回退阈值 |
| `RFLOW_MPP_ENC_TRACE_EVERY_N` | int | 45 | [1,600] | - | MPP 编码 trace 每 N 帧一行 |
| `RFLOW_MPP_ENC_ENABLE_IDR_CTRL` | bool | 0 | - | - | 启用 MPP CFG IDR 控制（实验） |

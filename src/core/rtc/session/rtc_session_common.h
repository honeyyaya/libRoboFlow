#ifndef __RFLOW_CORE_RTC_SESSION_RTC_SESSION_COMMON_H__
#define __RFLOW_CORE_RTC_SESSION_RTC_SESSION_COMMON_H__

#include "rtc/stats/inbound_video_stats_aggregation.h"

#include "media/frame_types.h"

#include <api/peer_connection_interface.h>
#include <api/rtp_receiver_interface.h>

#include <chrono>
#include <cstdint>

namespace rflow::core::rtc {

/// 接收端视频 jitter buffer 默认下限（秒）；可被 RFLOW_RECEIVER_JITTER_MIN_DELAY_MS 覆盖。
constexpr double kDefaultReceiverVideoJitterBufferMinDelaySeconds = 0.02;

/// 从环境变量读取接收端视频 jitter 最小延迟（秒）。
double ReceiverVideoJitterMinDelaySeconds();

/// 拉流 PeerConnection 通用调优：压低 audio jitter buffer，避免无音频时占用。
void ApplyReceiverPeerConnectionDefaults(
    webrtc::PeerConnectionInterface::RTCConfiguration& config);

/// 为视频 RtpReceiver 设置 jitter buffer 最小延迟。
/// @p jitter_min_delay_ms >= 0 时使用配置值；< 0 时不调用（保留 WebRTC 默认）。
void ApplyVideoReceiverJitterMinDelayMs(webrtc::RtpReceiverInterface* receiver,
                                        int jitter_min_delay_ms);

/// 从 inbound 聚合结果计算平均 jitter buffer delay（毫秒）。
uint32_t AverageJitterBufferDelayMs(const InboundVideoRtpAggregation& agg);

struct InboundVideoCollectSnapshot {
    InboundVideoRtpAggregation agg;
    uint32_t rtt_ms = 0;
    uint32_t available_incoming_kbps = 0;
};

/// 阻塞采集 inbound 视频 stats 快照（含 ICE RTT / 可用带宽）。
bool SyncCollectInboundVideoSnapshot(
    webrtc::PeerConnectionInterface* pc,
    InboundVideoCollectSnapshot* out,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

/// 将 inbound 聚合字段写入 stream stats（不含 bitrate；bitrate 由 tracker 单独更新）。
void FillInboundVideoStreamStats(librflow_stream_stats_s* out_stats,
                                 const InboundVideoRtpAggregation& agg,
                                 uint32_t rtt_ms,
                                 uint32_t jitter_min_delay_ms);

/// 基于两次 CollectStats 间隔估算 inbound bitrate；无增量时回退到 ICE 可用带宽。
struct InboundBitrateTracker {
    uint64_t prev_bytes = 0;
    int64_t prev_mono_ms = 0;
    uint32_t last_kbps = 0;

    uint32_t Update(uint64_t bytes_received,
                    int64_t now_mono_ms,
                    uint32_t fallback_kbps);
};

}  // namespace rflow::core::rtc

#endif  // __RFLOW_CORE_RTC_SESSION_RTC_SESSION_COMMON_H__

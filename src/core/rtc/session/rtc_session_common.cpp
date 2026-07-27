#include "rtc/session/rtc_session_common.h"

#include "rtc/session/rtc_sync_stats.h"

#include "base/env_reader.h"
#include "base/mono_time.h"

#include <algorithm>
#include <optional>

namespace rflow::core::rtc {

double ReceiverVideoJitterMinDelaySeconds() {
    static const int ms =
        rflow::common::util::ReadEnvIntInRange("RFLOW_RECEIVER_JITTER_MIN_DELAY_MS", 20, 0, 1000);
    return static_cast<double>(ms) / 1000.0;
}

void ApplyReceiverPeerConnectionDefaults(
    webrtc::PeerConnectionInterface::RTCConfiguration& config) {
    config.audio_jitter_buffer_max_packets = 1;
    config.audio_jitter_buffer_min_delay_ms = 0;
    config.audio_jitter_buffer_fast_accelerate = true;
}

void ApplyVideoReceiverJitterMinDelayMs(webrtc::RtpReceiverInterface* receiver,
                                        int jitter_min_delay_ms) {
    if (!receiver || jitter_min_delay_ms < 0) {
        return;
    }
    receiver->SetJitterBufferMinimumDelay(
        std::optional<double>(static_cast<double>(jitter_min_delay_ms) / 1000.0));
}

uint32_t AverageJitterBufferDelayMs(const InboundVideoRtpAggregation& agg) {
    if (agg.jitter_buffer_emitted_count == 0) {
        return 0;
    }
    return static_cast<uint32_t>(
        agg.jitter_buffer_delay_seconds * 1000.0 /
            static_cast<double>(agg.jitter_buffer_emitted_count) +
        0.5);
}

bool SyncCollectInboundVideoSnapshot(
    webrtc::PeerConnectionInterface* pc,
    InboundVideoCollectSnapshot* out,
    std::chrono::milliseconds timeout) {
    if (!pc || !out) {
        return false;
    }
    if (timeout.count() <= 0) {
        timeout = SyncGetPeerConnectionStatsTimeout();
    }
    return SyncGetPeerConnectionStats(
        pc, timeout,
        [out](const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
            AccumulateInboundVideoRtpFromReport(report, &out->agg);
            AccumulateIncomingIcePairMetricsFromReport(report, &out->rtt_ms,
                                                       &out->available_incoming_kbps);
        });
}

void FillInboundVideoStreamStats(librflow_stream_stats_s* out_stats,
                                 const InboundVideoRtpAggregation& agg,
                                 uint32_t rtt_ms,
                                 uint32_t jitter_min_delay_ms) {
    if (!out_stats) {
        return;
    }
    out_stats->in_bound_bytes = agg.bytes_received;
    out_stats->in_bound_pkts = agg.packets_received;
    out_stats->lost_pkts = agg.lost_pkts_positive_sum;
    out_stats->fps = agg.fps_uint;
    out_stats->jitter_ms = agg.jitter_ms_max;
    out_stats->freeze_count = agg.freeze_count_max;
    out_stats->decode_fail_count = static_cast<uint32_t>(
        std::min<uint64_t>(agg.frames_dropped_max, static_cast<uint64_t>(UINT32_MAX)));
    out_stats->rtt_ms = rtt_ms;
    out_stats->jitter_buffer_delay_ms = AverageJitterBufferDelayMs(agg);
    out_stats->jitter_min_delay_ms = jitter_min_delay_ms;
}

uint32_t InboundBitrateTracker::Update(uint64_t bytes_received,
                                       int64_t now_mono_ms,
                                       uint32_t fallback_kbps) {
    uint32_t kbps = 0;
    if (prev_mono_ms > 0 && now_mono_ms > prev_mono_ms && bytes_received >= prev_bytes) {
        const uint64_t d_bytes = bytes_received - prev_bytes;
        const int64_t d_ms = now_mono_ms - prev_mono_ms;
        kbps = static_cast<uint32_t>((d_bytes * 8ULL) / static_cast<uint64_t>(d_ms));
    } else if (fallback_kbps > 0) {
        kbps = fallback_kbps;
    }
    prev_bytes = bytes_received;
    prev_mono_ms = now_mono_ms;
    last_kbps = kbps;
    return kbps;
}

}  // namespace rflow::core::rtc

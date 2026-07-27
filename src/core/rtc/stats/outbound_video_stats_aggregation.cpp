#include "rtc/stats/outbound_video_stats_aggregation.h"

#include <algorithm>

#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"

namespace rflow::core::rtc {

void AccumulateOutboundPublisherVideoFromReport(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report,
    OutboundPublisherVideoAggregation* out) {
    if (!report || !out) {
        return;
    }

    for (const auto* outbound : report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>()) {
        if (!outbound || !outbound->kind || *outbound->kind != "video") continue;
        if (outbound->bytes_sent) out->out_bytes += *outbound->bytes_sent;
        if (outbound->packets_sent) out->out_pkts += *outbound->packets_sent;
        if (outbound->frames_per_second) {
            out->fps =
                std::max(out->fps,
                         static_cast<uint32_t>(*outbound->frames_per_second + 0.5));
        }
    }
    for (const auto* remote_in : report->GetStatsOfType<webrtc::RTCRemoteInboundRtpStreamStats>()) {
        if (!remote_in || !remote_in->kind || *remote_in->kind != "video") continue;
        if (remote_in->packets_lost && *remote_in->packets_lost > 0) {
            out->lost_pkts += static_cast<uint32_t>(*remote_in->packets_lost);
        }
        if (remote_in->round_trip_time) {
            out->rtt_ms = std::max(
                out->rtt_ms,
                static_cast<uint32_t>(*remote_in->round_trip_time * 1000.0 + 0.5));
        }
    }
    for (const auto* pair : report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>()) {
        if (!pair) continue;
        if (pair->current_round_trip_time) {
            out->rtt_ms = std::max(
                out->rtt_ms,
                static_cast<uint32_t>(*pair->current_round_trip_time * 1000.0 + 0.5));
        }
        if (pair->available_outgoing_bitrate) {
            out->bitrate_kbps =
                std::max(out->bitrate_kbps,
                         static_cast<uint32_t>(*pair->available_outgoing_bitrate / 1000.0 + 0.5));
        }
    }
}

}  // namespace rflow::core::rtc

#include "rtc/inbound_video_stats_aggregation.h"

#include <algorithm>

#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"

namespace rflow::core::rtc {

void AccumulateInboundVideoRtpFromReport(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report,
    InboundVideoRtpAggregation* out) {
    if (!report || !out) {
        return;
    }

    for (const auto* inbound : report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>()) {
        if (!inbound || !inbound->kind || *inbound->kind != "video") {
            continue;
        }

        if (inbound->bytes_received) {
            out->bytes_received += *inbound->bytes_received;
        }
        if (inbound->packets_received) {
            out->packets_received += static_cast<uint64_t>(*inbound->packets_received);
        }
        if (inbound->packets_lost) {
            const int32_t pl32 = *inbound->packets_lost;
            out->packets_lost_signed += static_cast<int64_t>(pl32);
            if (pl32 > 0) {
                out->lost_pkts_positive_sum += static_cast<uint32_t>(pl32);
            }
        }
        if (inbound->frames_decoded) {
            out->frames_decoded += static_cast<uint64_t>(*inbound->frames_decoded);
        }
        const uint64_t fd = inbound->frames_dropped
                                ? static_cast<uint64_t>(*inbound->frames_dropped)
                                : 0;
        out->frames_dropped_sum += fd;
        out->frames_dropped_max = std::max(out->frames_dropped_max, fd);
        if (inbound->frames_per_second) {
            const double fpsd = *inbound->frames_per_second;
            out->fps_double = std::max(out->fps_double, fpsd);
            out->fps_uint =
                std::max(out->fps_uint,
                         static_cast<uint32_t>(*inbound->frames_per_second + 0.5));
        }
        if (inbound->jitter) {
            out->jitter_seconds_max =
                std::max(out->jitter_seconds_max, static_cast<double>(*inbound->jitter));
            out->jitter_ms_max = std::max(
                out->jitter_ms_max,
                static_cast<uint32_t>(*inbound->jitter * 1000.0 + 0.5));
        }
        if (inbound->freeze_count) {
            out->freeze_count_max = std::max(out->freeze_count_max, *inbound->freeze_count);
        }
        if (inbound->jitter_buffer_delay) {
            out->jitter_buffer_delay_seconds += *inbound->jitter_buffer_delay;
        }
        if (inbound->jitter_buffer_emitted_count) {
            out->jitter_buffer_emitted_count += *inbound->jitter_buffer_emitted_count;
        }
        if (inbound->fec_packets_received) {
            out->fec_packets_received += *inbound->fec_packets_received;
        }
        if (inbound->nack_count) {
            out->nack_count_max = std::max(out->nack_count_max, *inbound->nack_count);
        }
        if (inbound->pli_count) {
            out->pli_count_max = std::max(out->pli_count_max, *inbound->pli_count);
        }
        if (inbound->fir_count) {
            out->fir_count_max = std::max(out->fir_count_max, *inbound->fir_count);
        }
        if (inbound->frame_width) {
            out->frame_width = std::max(out->frame_width, *inbound->frame_width);
        }
        if (inbound->frame_height) {
            out->frame_height = std::max(out->frame_height, *inbound->frame_height);
        }
        if (inbound->total_decode_time) {
            out->total_decode_time_s += *inbound->total_decode_time;
        }
        if (inbound->total_processing_delay) {
            out->total_processing_delay_s += *inbound->total_processing_delay;
        }
        if (inbound->total_assembly_time) {
            out->total_assembly_time_s += *inbound->total_assembly_time;
        }
        if (inbound->decoder_implementation && out->decoder_implementation.empty()) {
            out->decoder_implementation = *inbound->decoder_implementation;
        }
        if (inbound->codec_id && out->codec_id.empty()) {
            out->codec_id = *inbound->codec_id;
        }
        out->have_video = true;
    }
}

void AccumulateIncomingIcePairMetricsFromReport(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report,
    uint32_t* rtt_ms_max,
    uint32_t* available_incoming_kbps_max) {
    if (!report || !rtt_ms_max || !available_incoming_kbps_max) {
        return;
    }
    for (const auto* pair : report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>()) {
        if (!pair) {
            continue;
        }
        if (pair->current_round_trip_time) {
            *rtt_ms_max = std::max(
                *rtt_ms_max,
                static_cast<uint32_t>(*pair->current_round_trip_time * 1000.0 + 0.5));
        }
        if (pair->available_incoming_bitrate) {
            *available_incoming_kbps_max = std::max(
                *available_incoming_kbps_max,
                static_cast<uint32_t>(*pair->available_incoming_bitrate / 1000.0 + 0.5));
        }
    }
}

}  // namespace rflow::core::rtc

#include "media/rtc/video_rtp_stats_log.h"

#include <cerrno>
#include <sstream>
#include <vector>

#include "api/stats/rtcstats_objects.h"

#include "public/log_tagged.h"

namespace rflow::service::impl::media_util {
namespace {

std::vector<std::string> SplitCommaFields(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t comma = s.find(',', start);
        if (comma == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, comma - start));
        start = comma + 1;
    }
    return out;
}

bool ParseInt64Strict(const std::string& s, int64_t* out) {
    if (!out || s.empty()) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0 || end != s.c_str() + s.size()) {
        return false;
    }
    *out = static_cast<int64_t>(v);
    return true;
}

void PrintTimingFrameDerivedDeltas(const std::vector<std::string>& parts) {
    constexpr size_t kN = 15;
    if (parts.size() != kN) {
        return;
    }
    int64_t cap = 0;
    int64_t enc_s = 0;
    int64_t enc_f = 0;
    int64_t recv_s = 0;
    int64_t recv_f = 0;
    int64_t dec_s = 0;
    int64_t dec_f = 0;
    if (!ParseInt64Strict(parts[1], &cap) || !ParseInt64Strict(parts[2], &enc_s) ||
        !ParseInt64Strict(parts[3], &enc_f) || !ParseInt64Strict(parts[8], &recv_s) ||
        !ParseInt64Strict(parts[9], &recv_f) || !ParseInt64Strict(parts[10], &dec_s) ||
        !ParseInt64Strict(parts[11], &dec_f)) {
        RFLOW_LOG_TAG_W("TimingDelta", "numeric parse failed, skipping delta derivation");
        return;
    }

    RFLOW_LOG_TAG_I("TimingDelta", "receiver uses same local clock (decode_* and receive_* can be subtracted directly):");
    RFLOW_LOG_TAG_I("TimingDelta", "  first_RTP→decode_start = %lld ms (first RTP arrival → decode start; includes network jitter, JB, frame assembly, scheduling)",
                    static_cast<long long>(dec_s - recv_s));
    RFLOW_LOG_TAG_I("TimingDelta", "  last_RTP→decode_start = %lld ms (last RTP for frame → decode start; mainly JB/pre-decoder queueing)",
                    static_cast<long long>(dec_s - recv_f));
    RFLOW_LOG_TAG_I("TimingDelta", "  decode_start→decode_finish = %lld ms (decode duration for this frame)",
                    static_cast<long long>(dec_f - dec_s));
    RFLOW_LOG_TAG_I("TimingDelta", "  RTP_last−first = %lld ms (multi-packet arrival span for this frame)",
                    static_cast<long long>(recv_f - recv_s));

    if (cap >= 0) {
        RFLOW_LOG_TAG_I("TimingDelta", "end-to-end (WebRTC aligned sender time for receiver comparison; capture_time_ms>=0):");
        RFLOW_LOG_TAG_I("TimingDelta", "  capture→decode_start = %lld ms (sender capture → receiver decode start)",
                        static_cast<long long>(dec_s - cap));
    } else {
        RFLOW_LOG_TAG_W(
            "TimingDelta",
            "capture_time_ms<0: sender/receiver absolute times not aligned; do not use decode_start−capture as full "
            "end-to-end. Use first_RTP→decode_start for post-arrival latency, or stamp unified clock in app layer.");
    }

    RFLOW_LOG_TAG_I("TimingDelta", "sender-side relative metrics (differences still meaningful under same offset):");
    RFLOW_LOG_TAG_I("TimingDelta", "  encode_start−capture = %lld ms", static_cast<long long>(enc_s - cap));
    RFLOW_LOG_TAG_I("TimingDelta", "  encode_finish−encode_start = %lld ms (approx. encode duration for this frame)",
                    static_cast<long long>(enc_f - enc_s));
}

void PrintGoogTimingFrameInfoLabeled(const std::string& raw) {
    static constexpr const char* kFieldLabels[] = {
        "rtp_timestamp — RTP timestamp (90kHz clock units, not milliseconds)",
        "capture_time_ms — sender: capture time (ms); negative on receiver when NTP not aligned, relative diffs still valid",
        "encode_start_ms — sender: encode start (ms)",
        "encode_finish_ms — sender: encode finish (ms)",
        "packetization_finish_ms — sender: packetization done / before pacer (ms)",
        "pacer_exit_ms — sender: last packet left pacer for this frame (ms); -1 if unused",
        "network_timestamp_ms — sender/ext: in-network timestamp 1 (ms)",
        "network2_timestamp_ms — sender/ext: in-network timestamp 2 (ms)",
        "receive_start_ms — receiver local clock: first RTP packet arrival for frame (ms)",
        "receive_finish_ms — receiver local clock: last packet received for frame (ms)",
        "decode_start_ms — receiver local clock: decode start (ms)",
        "decode_finish_ms — receiver local clock: decode finish (ms)",
        "render_time_ms — receiver: suggested render time (ms)",
        "is_outlier — outlier by frame size (0/1)",
        "is_timer_triggered — selected as timing frame by periodic timer (0/1)",
    };
    constexpr size_t kN = sizeof(kFieldLabels) / sizeof(kFieldLabels[0]);
    const std::vector<std::string> parts = SplitCommaFields(raw);
    if (parts.size() != kN) {
        RFLOW_LOG_TAG_I("VideoTiming", "TimingFrameInfo (raw): %s", raw.c_str());
        RFLOW_LOG_TAG_W("VideoTiming", "field count=%zu (expected %zu), does not match current libwebrtc ToString format",
                        parts.size(), kN);
        return;
    }
    RFLOW_LOG_TAG_I("VideoTiming", "TimingFrameInfo field-by-field (goog_timing_frame_info):");
    for (size_t i = 0; i < kN; ++i) {
        RFLOW_LOG_TAG_I("VideoTiming", "  [%zu] %s = %s", i + 1, kFieldLabels[i], parts[i].c_str());
    }
    PrintTimingFrameDerivedDeltas(parts);
}

}  // namespace

void PrintOutboundVideoStats(
    const std::string& pc_tag,
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
    if (!report) {
        return;
    }
    const std::vector<const webrtc::RTCOutboundRtpStreamStats*> outbound =
        report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>();
    for (const webrtc::RTCOutboundRtpStreamStats* s : outbound) {
        if (!s->kind.has_value() || *s->kind != "video") {
            continue;
        }
        std::ostringstream line;
        line << "pc=" << pc_tag << " id=" << s->id();
        if (s->ssrc.has_value()) {
            line << " ssrc=" << *s->ssrc;
        }
        if (s->frames_encoded.has_value()) {
            line << " frames_encoded=" << *s->frames_encoded;
        }
        if (s->key_frames_encoded.has_value()) {
            line << " key_frames_encoded=" << *s->key_frames_encoded;
        }
        if (s->packets_sent.has_value()) {
            line << " packets_sent=" << *s->packets_sent;
        }
        if (s->bytes_sent.has_value()) {
            line << " bytes_sent=" << *s->bytes_sent;
        }
        if (s->retransmitted_packets_sent.has_value()) {
            line << " retrans_pkts=" << *s->retransmitted_packets_sent;
        }
        if (s->frame_width.has_value()) {
            line << " frame_w=" << *s->frame_width;
        }
        if (s->frame_height.has_value()) {
            line << " frame_h=" << *s->frame_height;
        }
        if (s->quality_limitation_reason.has_value()) {
            line << " ql_reason=" << *s->quality_limitation_reason;
        }
        if (s->quality_limitation_resolution_changes.has_value()) {
            line << " ql_res_changes=" << *s->quality_limitation_resolution_changes;
        }
        RFLOW_LOG_TAG_I("OutboundVideoStats", "%s", line.str().c_str());
    }
}

void PrintInboundVideoStats(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
    if (!report) {
        return;
    }
    const std::vector<const webrtc::RTCInboundRtpStreamStats*> inbound =
        report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>();
    for (const webrtc::RTCInboundRtpStreamStats* s : inbound) {
        if (!s->kind.has_value() || *s->kind != "video") {
            continue;
        }
        std::ostringstream line;
        line << "id=" << s->id();
        if (s->ssrc.has_value()) {
            line << " ssrc=" << *s->ssrc;
        }
        if (s->frames_decoded.has_value()) {
            line << " frames_decoded=" << *s->frames_decoded;
        }
        if (s->frames_received.has_value()) {
            line << " frames_received=" << *s->frames_received;
        }
        if (s->total_decode_time.has_value()) {
            line << " total_decode_time_s=" << *s->total_decode_time;
        }
        if (s->total_processing_delay.has_value()) {
            line << " total_processing_delay_s=" << *s->total_processing_delay;
        }
        if (s->jitter_buffer_delay.has_value()) {
            line << " jitter_buffer_delay_s=" << *s->jitter_buffer_delay;
        }
        RFLOW_LOG_TAG_I("InboundVideoStats", "%s", line.str().c_str());
        if (s->goog_timing_frame_info.has_value() && !s->goog_timing_frame_info->empty()) {
            PrintGoogTimingFrameInfoLabeled(*s->goog_timing_frame_info);
        } else {
            RFLOW_LOG_TAG_I(
                "VideoTiming",
                "goog_timing_frame_info: (empty — peer did not send video-timing extension or no timing frame selected yet)");
        }
    }
}

}  // namespace rflow::service::impl::media_util

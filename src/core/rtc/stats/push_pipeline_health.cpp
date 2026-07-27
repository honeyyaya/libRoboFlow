#include "rtc/stats/push_pipeline_health.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "public/log_tagged.h"
#include "rtc/stats/push_pipeline_drop_stats.h"
#include "runtime/runtime_knobs.h"

namespace rflow::core::rtc {

namespace {

struct HealthBaseline {
    uint64_t capture{0};
    uint64_t dispatched{0};
    uint64_t mjpeg_stale{0};
    uint64_t mjpeg_qfull{0};
    uint64_t mjpeg_latest{0};
    uint64_t empty_payload{0};
    uint64_t convert_fail{0};
    uint64_t nv12_pool_busy{0};
    uint64_t rga_scale_ok{0};
    uint64_t rga_scale_fail{0};
    uint64_t zc_fallback{0};
    uint64_t rtp_lost_pkts{0};
    uint32_t frames_encoded{0};
    bool initialized{false};
};

uint32_t FpsFromDelta(uint64_t delta, int interval_sec) {
    if (interval_sec <= 0) {
        return 0;
    }
    return static_cast<uint32_t>((delta + static_cast<uint64_t>(interval_sec) / 2) /
                                 static_cast<uint64_t>(interval_sec));
}

void ExtractOutboundVideoFields(const webrtc::RTCStatsReport* report,
                                uint32_t* frames_encoded,
                                uint32_t* frame_w,
                                uint32_t* frame_h,
                                uint32_t* enc_fps,
                                const char** ql_reason,
                                uint32_t* rtt_ms,
                                uint32_t* bitrate_kbps,
                                uint64_t* rtp_lost_pkts) {
    if (!report) {
        return;
    }
    for (const webrtc::RTCOutboundRtpStreamStats* s :
         report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>()) {
        if (!s->kind.has_value() || *s->kind != "video") {
            continue;
        }
        if (s->frames_encoded.has_value()) {
            *frames_encoded = std::max(*frames_encoded, *s->frames_encoded);
        }
        if (s->frame_width.has_value()) {
            *frame_w = std::max(*frame_w, *s->frame_width);
        }
        if (s->frame_height.has_value()) {
            *frame_h = std::max(*frame_h, *s->frame_height);
        }
        if (s->frames_per_second.has_value()) {
            *enc_fps = std::max(*enc_fps, static_cast<uint32_t>(*s->frames_per_second + 0.5));
        }
        if (s->quality_limitation_reason.has_value()) {
            *ql_reason = s->quality_limitation_reason->c_str();
        }
    }
    for (const webrtc::RTCRemoteInboundRtpStreamStats* s :
         report->GetStatsOfType<webrtc::RTCRemoteInboundRtpStreamStats>()) {
        if (!s->kind.has_value() || *s->kind != "video") {
            continue;
        }
        if (s->round_trip_time.has_value()) {
            *rtt_ms = std::max(*rtt_ms, static_cast<uint32_t>(*s->round_trip_time * 1000.0 + 0.5));
        }
        if (s->packets_lost.has_value() && *s->packets_lost > 0) {
            *rtp_lost_pkts += static_cast<uint64_t>(*s->packets_lost);
        }
    }
    for (const webrtc::RTCIceCandidatePairStats* pair :
         report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>()) {
        if (pair->current_round_trip_time.has_value()) {
            *rtt_ms = std::max(*rtt_ms,
                               static_cast<uint32_t>(*pair->current_round_trip_time * 1000.0 + 0.5));
        }
        if (pair->available_outgoing_bitrate.has_value()) {
            *bitrate_kbps = std::max(
                *bitrate_kbps, static_cast<uint32_t>(*pair->available_outgoing_bitrate / 1000.0 + 0.5));
        }
    }
}

const char* QlReasonLabel(const char* ql_reason) {
    if (!ql_reason || ql_reason[0] == '\0' || std::strcmp(ql_reason, "none") == 0) {
        return "none";
    }
    return ql_reason;
}

void AppendDropPart(char* buf, size_t cap, const char* label, uint64_t value) {
    if (!buf || cap == 0 || value == 0) {
        return;
    }
    const size_t used = std::strlen(buf);
    if (used >= cap - 1) {
        return;
    }
    const int n = snprintf(buf + used, cap - used, "%s%s%llu", used > 0 ? ", " : "", label,
                           static_cast<unsigned long long>(value));
    if (n < 0 || static_cast<size_t>(n) >= cap - used) {
        buf[cap - 1] = '\0';
    }
}

void FormatWindowDrops(char* buf,
                       size_t cap,
                       uint64_t stale_delta,
                       uint64_t qfull_delta,
                       uint64_t latest_delta,
                       uint64_t empty_delta,
                       uint64_t convert_delta,
                       uint64_t nv12_busy_delta) {
    if (!buf || cap == 0) {
        return;
    }
    buf[0] = '\0';
    AppendDropPart(buf, cap, "stale", stale_delta);
    AppendDropPart(buf, cap, "queue_full", qfull_delta);
    AppendDropPart(buf, cap, "latest_only", latest_delta);
    AppendDropPart(buf, cap, "empty_payload", empty_delta);
    AppendDropPart(buf, cap, "convert_fail", convert_delta);
    AppendDropPart(buf, cap, "nv12_pool_busy", nv12_busy_delta);
    if (buf[0] == '\0') {
        snprintf(buf, cap, "none");
    }
}

const char* BacklogTrend(int64_t gap_delta) {
    if (gap_delta > 0) {
        return "rising";
    }
    if (gap_delta < 0) {
        return "falling";
    }
    return "stable";
}

const char* HealthStatus(bool has_drop, bool backlog_growing, bool ql_active) {
    if (has_drop && backlog_growing) {
        return "DROPS+BACKLOG";
    }
    if (has_drop) {
        return "DROPS";
    }
    if (backlog_growing) {
        return "BACKLOG";
    }
    if (ql_active) {
        return "THROTTLED";
    }
    return "OK";
}

}  // namespace

void LogPushPipelineHealth(int interval_sec,
                           const CapturePipelineHealthInput& cam,
                           const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
    if (interval_sec <= 0) {
        return;
    }
    static HealthBaseline prev;

    const PushPipelineDropStats& drop_stats = PushPipelineDropStats::Instance();
    const uint64_t capture = drop_stats.v4l2_capture_total();
    const uint64_t dispatched = drop_stats.dispatched_total();
    const uint64_t gap = capture >= dispatched ? capture - dispatched : 0;

    uint32_t frames_encoded = 0;
    uint32_t frame_w = 0;
    uint32_t frame_h = 0;
    uint32_t enc_fps = 0;
    const char* ql_reason = "none";
    uint32_t rtt_ms = 0;
    uint32_t bitrate_kbps = 0;
    uint64_t rtp_lost_pkts = 0;
    ExtractOutboundVideoFields(report.get(), &frames_encoded, &frame_w, &frame_h, &enc_fps, &ql_reason, &rtt_ms,
                               &bitrate_kbps, &rtp_lost_pkts);

    const uint64_t rga_ok_total = drop_stats.rga_scale_ok_total();
    const uint64_t rga_fail_total = drop_stats.rga_scale_fail_total();
    const uint64_t zc_fallback_total = drop_stats.zc_fallback_total();

    uint64_t cap_delta = 0;
    uint64_t disp_delta = 0;
    int64_t gap_delta = 0;
    uint64_t stale_delta = 0;
    uint64_t qfull_delta = 0;
    uint64_t latest_delta = 0;
    uint64_t empty_delta = 0;
    uint64_t convert_delta = 0;
    uint64_t nv12_busy_delta = 0;
    uint64_t rga_ok_delta = 0;
    uint64_t rga_fail_delta = 0;
    uint64_t zc_fallback_delta = 0;
    uint64_t rtp_lost_delta = 0;
    uint32_t enc_delta = 0;

    if (prev.initialized) {
        cap_delta = capture >= prev.capture ? capture - prev.capture : 0;
        disp_delta = dispatched >= prev.dispatched ? dispatched - prev.dispatched : 0;
        const uint64_t prev_gap = prev.capture >= prev.dispatched ? prev.capture - prev.dispatched : 0;
        gap_delta = static_cast<int64_t>(gap) - static_cast<int64_t>(prev_gap);
        stale_delta = cam.mjpeg_stale_drops >= prev.mjpeg_stale ? cam.mjpeg_stale_drops - prev.mjpeg_stale : 0;
        qfull_delta =
            cam.mjpeg_queue_full_drops >= prev.mjpeg_qfull ? cam.mjpeg_queue_full_drops - prev.mjpeg_qfull : 0;
        latest_delta = cam.mjpeg_latest_only_drops >= prev.mjpeg_latest
                           ? cam.mjpeg_latest_only_drops - prev.mjpeg_latest
                           : 0;
        empty_delta = cam.empty_payload_drops >= prev.empty_payload ? cam.empty_payload_drops - prev.empty_payload
                                                                      : 0;
        convert_delta =
            cam.convert_fail_drops >= prev.convert_fail ? cam.convert_fail_drops - prev.convert_fail : 0;
        nv12_busy_delta =
            cam.nv12_pool_busy >= prev.nv12_pool_busy ? cam.nv12_pool_busy - prev.nv12_pool_busy : 0;
        rga_ok_delta = rga_ok_total >= prev.rga_scale_ok ? rga_ok_total - prev.rga_scale_ok : 0;
        rga_fail_delta = rga_fail_total >= prev.rga_scale_fail ? rga_fail_total - prev.rga_scale_fail : 0;
        zc_fallback_delta =
            zc_fallback_total >= prev.zc_fallback ? zc_fallback_total - prev.zc_fallback : 0;
        rtp_lost_delta = rtp_lost_pkts >= prev.rtp_lost_pkts ? rtp_lost_pkts - prev.rtp_lost_pkts : 0;
        enc_delta = frames_encoded >= prev.frames_encoded ? frames_encoded - prev.frames_encoded : 0;
    }

    prev.capture = capture;
    prev.dispatched = dispatched;
    prev.mjpeg_stale = cam.mjpeg_stale_drops;
    prev.mjpeg_qfull = cam.mjpeg_queue_full_drops;
    prev.mjpeg_latest = cam.mjpeg_latest_only_drops;
    prev.empty_payload = cam.empty_payload_drops;
    prev.convert_fail = cam.convert_fail_drops;
    prev.nv12_pool_busy = cam.nv12_pool_busy;
    prev.rga_scale_ok = rga_ok_total;
    prev.rga_scale_fail = rga_fail_total;
    prev.zc_fallback = zc_fallback_total;
    prev.rtp_lost_pkts = rtp_lost_pkts;
    prev.frames_encoded = frames_encoded;
    if (!prev.initialized) {
        prev.initialized = true;
        return;
    }

    const bool has_drop = stale_delta > 0 || qfull_delta > 0 || latest_delta > 0 || empty_delta > 0 ||
                          convert_delta > 0 || nv12_busy_delta > 0;
    const bool encode_pressure = rga_fail_delta > 0 || zc_fallback_delta > 0;
    const bool network_loss = rtp_lost_delta > 0;
    const bool backlog_growing = gap_delta > 0 || disp_delta + 2 < cap_delta;
    const bool ql_active = ql_reason && ql_reason[0] != '\0' && std::strcmp(ql_reason, "none") != 0;

    const bool only_anomaly = rflow::core::runtime::ReadInt("RFLOW_PUSH_HEALTH_ONLY_ANOMALY") != 0;
    if (only_anomaly && !has_drop && !backlog_growing && !ql_active && !encode_pressure && !network_loss) {
        return;
    }

    const uint32_t cap_fps = FpsFromDelta(cap_delta, interval_sec);
    const uint32_t disp_fps = FpsFromDelta(disp_delta, interval_sec);
    char drops_buf[192];
    FormatWindowDrops(drops_buf, sizeof(drops_buf), stale_delta, qfull_delta, latest_delta, empty_delta,
                      convert_delta, nv12_busy_delta);

    RFLOW_LOG_TAG_I("PushHealth", "---- pipeline health (%ds) status=%s ----", interval_sec,
                    HealthStatus(has_drop, backlog_growing, ql_active));
    RFLOW_LOG_TAG_I(
        "PushHealth",
        "  [capture] total=%llu | window=+%llu (~%ufps) -> [webrtc_in] total=%llu | window=+%llu (~%ufps)",
        static_cast<unsigned long long>(capture), static_cast<unsigned long long>(cap_delta), cap_fps,
        static_cast<unsigned long long>(dispatched), static_cast<unsigned long long>(disp_delta), disp_fps);
    RFLOW_LOG_TAG_I("PushHealth",
                    "  [backlog] gap=%llu frames | trend=%s | mjpeg_qdepth=%zu | window_drops: %s",
                    static_cast<unsigned long long>(gap), BacklogTrend(gap_delta), cam.mjpeg_queue_depth,
                    drops_buf);

    if (frame_w > 0 && frame_h > 0) {
        RFLOW_LOG_TAG_I("PushHealth",
                        "  [send] window_encoded=+%u | enc_fps=%u | resolution=%ux%u | ql_reason=%s | "
                        "rga=+%llu/+%llu | zc_fallback=+%llu | rtp_lost=+%llu | avail_bitrate=%ukbps rtt=%ums",
                        enc_delta, enc_fps, frame_w, frame_h, QlReasonLabel(ql_reason),
                        static_cast<unsigned long long>(rga_ok_delta),
                        static_cast<unsigned long long>(rga_fail_delta),
                        static_cast<unsigned long long>(zc_fallback_delta),
                        static_cast<unsigned long long>(rtp_lost_delta), bitrate_kbps, rtt_ms);
    } else {
        RFLOW_LOG_TAG_I("PushHealth",
                        "  [send] window_encoded=+%u | enc_fps=%u | webrtc_not_ready | ql_reason=%s | "
                        "rga=+%llu/+%llu | zc_fallback=+%llu | rtp_lost=+%llu",
                        enc_delta, enc_fps, QlReasonLabel(ql_reason),
                        static_cast<unsigned long long>(rga_ok_delta),
                        static_cast<unsigned long long>(rga_fail_delta),
                        static_cast<unsigned long long>(zc_fallback_delta),
                        static_cast<unsigned long long>(rtp_lost_delta));
    }
}

}  // namespace rflow::core::rtc

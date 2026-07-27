#include "media/push/push_streamer_internals.h"

#include "rtc/rtc_ice_config.h"

#include <algorithm>
#include <cstdio>

#include "api/stats/rtcstats_objects.h"
#include "media/push/camera_video_track_source.h"
#include "rtc/push_pipeline_health.h"

#include "base/trace_switches.h"
#include "rtc/push_pipeline_drop_stats.h"

namespace rflow::service::impl::detail::push {

webrtc::PeerConnectionInterface::RTCConfiguration MakeRtcConfiguration(
    const PushStreamerConfig& config) {
    rflow::rtc::IceRtcServerConfig ice;
    ice.has_stun_server               = config.common.has_stun_server;
    ice.stun_server                   = config.common.stun_server;
    ice.has_turn_server               = config.common.has_turn_server;
    ice.turn_server                   = config.common.turn_server;
    ice.turn_username                 = config.common.turn_username;
    ice.turn_password                 = config.common.turn_password;
    ice.ice_prioritize_likely_pairs = config.common.ice_prioritize_likely_pairs;
    return rflow::rtc::BuildRtcConfiguration(ice);
}

void MaybeLogWebRtcSendDropStats(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
    if (!report) {
        return;
    }
    uint32_t frames_encoded = 0;
    const std::vector<const webrtc::RTCOutboundRtpStreamStats*> outbound =
        report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>();
    const char* ql_reason = "none";
    uint32_t frame_w = 0;
    uint32_t frame_h = 0;
    for (const webrtc::RTCOutboundRtpStreamStats* s : outbound) {
        if (!s->kind.has_value() || *s->kind != "video") {
            continue;
        }
        if (s->frames_encoded.has_value()) {
            frames_encoded = std::max(frames_encoded, *s->frames_encoded);
        }
        if (s->quality_limitation_reason.has_value()) {
            ql_reason = s->quality_limitation_reason->c_str();
        }
        if (s->frame_width.has_value()) {
            frame_w = *s->frame_width;
        }
        if (s->frame_height.has_value()) {
            frame_h = *s->frame_height;
        }
    }
    static uint64_t prev_dispatched = 0;
    static uint64_t prev_encoded = 0;
    static uint64_t webrtc_drop_total = 0;
    const uint64_t dispatched =
        rflow::core::rtc::PushPipelineDropStats::Instance().dispatched_total();
    if (prev_dispatched == 0 && prev_encoded == 0) {
        prev_dispatched = dispatched;
        prev_encoded = frames_encoded;
        return;
    }
    const uint64_t delta_dispatched =
        dispatched >= prev_dispatched ? dispatched - prev_dispatched : 0;
    const uint64_t delta_encoded =
        frames_encoded >= prev_encoded ? static_cast<uint64_t>(frames_encoded - prev_encoded) : 0;
    prev_dispatched = dispatched;
    prev_encoded = frames_encoded;
    if (delta_dispatched <= delta_encoded) {
        return;
    }
    const uint64_t delta = delta_dispatched - delta_encoded;
    webrtc_drop_total += delta;
    char detail[160];
    snprintf(detail, sizeof(detail),
             "window_webrtc_in=%llu window_encoded=%llu | frames_encoded_total=%u | ql_reason=%s | frame=%ux%u",
             static_cast<unsigned long long>(delta_dispatched),
             static_cast<unsigned long long>(delta_encoded), frames_encoded, ql_reason, frame_w, frame_h);
    rflow::core::rtc::PushPipelineDropStats::Instance().LogDrop("WebRTC/Send", delta, webrtc_drop_total,
                                                                     detail);
}

void MaybeLogPushPipelineHealth(
    int interval_sec,
    const rflow::service::impl::CameraVideoTrackSource* camera,
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
    rflow::core::rtc::CapturePipelineHealthInput input;
    if (camera) {
        const auto snap = camera->GetPipelineHealthSnapshot();
        input.mjpeg_queue_depth = snap.mjpeg_queue_depth;
        input.mjpeg_stale_drops = snap.mjpeg_stale_drops;
        input.mjpeg_queue_full_drops = snap.mjpeg_queue_full_drops;
        input.mjpeg_latest_only_drops = snap.mjpeg_latest_only_drops;
        input.empty_payload_drops = snap.empty_payload_drops;
        input.convert_fail_drops = snap.convert_fail_drops;
        input.nv12_pool_busy = snap.nv12_pool_busy;
    }
    rflow::core::rtc::LogPushPipelineHealth(interval_sec, input, report);
}

}  // namespace rflow::service::impl::detail::push

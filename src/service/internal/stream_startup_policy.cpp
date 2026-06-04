#include "internal/stream_startup_policy.h"
#include "internal/service_default_params.h"

#include "rtc/hw/codec_mapping.h"
#include "rflow/librflow_common.h"

#if defined(RFLOW_SVC_WEBRTC_IMPL)
#include "impl/publisher.h"
#endif

namespace rflow::service::internal {

namespace {

const char* DegradationPreferenceToConfigLiteral(rflow_degradation_preference_t v) {
    switch (v) {
        case RFLOW_DEGRADATION_MAINTAIN_FRAMERATE:
            return "maintain_framerate";
        case RFLOW_DEGRADATION_MAINTAIN_RESOLUTION:
            return "maintain_resolution";
        case RFLOW_DEGRADATION_BALANCED:
            return "balanced";
        default:
            return nullptr;
    }
}

const char* NetworkPriorityToConfigLiteral(rflow_svc_network_priority_t v) {
    switch (v) {
        case RFLOW_SVC_NETWORK_PRIORITY_VERY_LOW:
            return "very_low";
        case RFLOW_SVC_NETWORK_PRIORITY_LOW:
            return "low";
        case RFLOW_SVC_NETWORK_PRIORITY_MEDIUM:
            return "medium";
        case RFLOW_SVC_NETWORK_PRIORITY_HIGH:
            return "high";
        default:
            return nullptr;
    }
}

}  // namespace

ResolvedPublisherStartup ResolvePublisherStartup(const librflow_svc_stream_s& stream,
                                                const State& state,
                                                int32_t stream_idx) {
    ResolvedPublisherStartup out;
    const ServiceDefaultParams& knobs = GetServiceDefaultParams();

    out.width = static_cast<int>(stream.param.has_out_size ? stream.param.out_w
                                                            : (stream.param.has_src_size ? stream.param.src_w : 0));
    out.height = static_cast<int>(stream.param.has_out_size ? stream.param.out_h
                                                             : (stream.param.has_src_size ? stream.param.src_h : 0));
    out.fps = static_cast<int>(stream.param.has_fps ? stream.param.fps : knobs.default_fps);

    out.bitrate_kbps_target =
        static_cast<int>(stream.param.has_bitrate ? stream.param.bitrate_kbps : knobs.default_bitrate_kbps);
    out.bitrate_kbps_max = static_cast<int>(stream.param.has_bitrate ? stream.param.max_bitrate_kbps : 0);
    if (out.bitrate_kbps_max == 0) {
        out.bitrate_kbps_max = knobs.default_max_bitrate_kbps;
    }
    if (out.bitrate_kbps_max == 0) {
        out.bitrate_kbps_max = out.bitrate_kbps_target;
    }
    out.bitrate_kbps_min =
        static_cast<int>(stream.param.has_dynamic_bitrate ? stream.param.lowest_kbps : knobs.default_min_bitrate_kbps);

    out.video_codec = rflow::rtc::hw::CodecToString(
        stream.param.has_out_codec ? stream.param.out_codec : RFLOW_CODEC_H264);
    out.use_internal_video_source =
        stream.param.has_video_device_path || stream.param.has_video_device_index || knobs.prefer_internal_video_source;
    out.video_device_path = stream.param.has_video_device_path ? stream.param.video_device_path : std::string();
    out.video_device_index =
        stream.param.has_video_device_index ? static_cast<int>(stream.param.video_device_index) : 0;

    out.signal_url = state.global_config.has_signal ? state.global_config.signal.url : std::string();
    out.device_id = state.connect_info.device_id.empty() ? std::string(RFLOW_DEFAULT_DEVICE_ID) : state.connect_info.device_id;
    out.stream_id = out.device_id + ":" + std::to_string(stream_idx);
    if (stream.param.has_degradation_preference) {
        if (const char* lit = DegradationPreferenceToConfigLiteral(stream.param.degradation_preference)) {
            out.degradation_pref_explicit = std::string(lit);
        }
    }
    if (stream.param.has_h264_profile) {
        out.h264_profile = stream.param.h264_profile;
    }
    if (stream.param.has_h264_level) {
        out.h264_level = stream.param.h264_level;
    }
    if (stream.param.has_gop) {
        out.keyframe_gop = static_cast<int>(stream.param.gop);
    }
    if (stream.param.has_ice_prioritize_likely_pairs) {
        out.ice_prioritize_likely_pairs = stream.param.ice_prioritize_likely_pairs;
    }
    if (stream.param.has_video_network_priority) {
        if (const char* lit = NetworkPriorityToConfigLiteral(stream.param.video_network_priority)) {
            out.video_network_priority = std::string(lit);
        }
    }
    if (stream.param.has_video_encoding_max_fps) {
        out.video_encoding_max_framerate = static_cast<int>(stream.param.video_encoding_max_fps);
    }
    if (stream.param.has_capture_warmup_sec) {
        out.capture_warmup_sec = static_cast<int>(stream.param.capture_warmup_sec);
    }
    if (stream.param.has_capture_gate) {
        out.capture_gate_min_frames     = static_cast<int>(stream.param.capture_gate_min_frames);
        out.capture_gate_max_wait_sec = static_cast<int>(stream.param.capture_gate_max_wait_sec);
    }
    if (stream.param.has_bitrate_mode) {
        out.bitrate_mode = stream.param.bitrate_mode;
    } else if (stream.param.has_rc_mode) {
        if (stream.param.rc_mode == RFLOW_RC_CBR) {
            out.bitrate_mode = RFLOW_BITRATE_MODE_CBR;
        } else if (stream.param.rc_mode == RFLOW_RC_VBR) {
            out.bitrate_mode = RFLOW_BITRATE_MODE_VBR;
        }
    }
    return out;
}

#if defined(RFLOW_SVC_WEBRTC_IMPL)
std::shared_ptr<void> CreatePublisherImplForStream(const librflow_svc_stream_s& stream,
                                                   const State& state,
                                                   int32_t stream_idx) {
    const auto startup = ResolvePublisherStartup(stream, state, stream_idx);
    rflow::service::impl::PublisherPullCallbacks cbs{};
    if (state.has_connect_cb) {
        cbs.on_pull_request   = state.connect_cb.on_pull_request;
        cbs.on_pull_release   = state.connect_cb.on_pull_release;
        cbs.on_connect_state  = state.connect_cb.on_state;
        cbs.userdata          = state.connect_cb.userdata;
    }
    rflow::service::impl::PublisherMediaOptions m;
    if (startup.h264_profile) {
        m.h264_profile = startup.h264_profile;
    }
    if (startup.h264_level) {
        m.h264_level = startup.h264_level;
    }
    if (startup.keyframe_gop.has_value()) {
        m.keyframe_gop_frames = startup.keyframe_gop;
    }
    if (startup.ice_prioritize_likely_pairs.has_value()) {
        m.ice_prioritize_likely_pairs = startup.ice_prioritize_likely_pairs;
    }
    if (startup.video_network_priority) {
        m.video_network_priority = startup.video_network_priority;
    }
    if (startup.video_encoding_max_framerate.has_value()) {
        m.video_encoding_max_framerate = startup.video_encoding_max_framerate;
    }
    if (startup.capture_warmup_sec.has_value()) {
        m.capture_warmup_sec = startup.capture_warmup_sec;
    }
    if (startup.capture_gate_min_frames.has_value() && startup.capture_gate_max_wait_sec.has_value()) {
        m.capture_gate_min_frames     = startup.capture_gate_min_frames;
        m.capture_gate_max_wait_sec = startup.capture_gate_max_wait_sec;
    }
    if (startup.bitrate_mode.has_value()) {
        m.bitrate_mode = startup.bitrate_mode;
    }
    return std::make_shared<rflow::service::impl::Publisher>(
        stream_idx,
        stream.param.has_in_codec ? stream.param.in_codec : RFLOW_CODEC_I420,
        startup.stream_id,
        startup.signal_url,
        startup.device_id,
        startup.width,
        startup.height,
        startup.fps,
        startup.bitrate_kbps_target,
        startup.bitrate_kbps_min,
        startup.bitrate_kbps_max,
        startup.video_codec,
        startup.use_internal_video_source,
        startup.video_device_path,
        startup.video_device_index,
        startup.degradation_pref_explicit,
        std::move(m),
        cbs);
}
#endif

}  // namespace rflow::service::internal

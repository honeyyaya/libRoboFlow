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
    return out;
}

#if defined(RFLOW_SVC_WEBRTC_IMPL)
std::shared_ptr<void> CreatePublisherImplForStream(const librflow_svc_stream_s& stream,
                                                   const State& state,
                                                   int32_t stream_idx) {
    const auto startup = ResolvePublisherStartup(stream, state, stream_idx);
    rflow::service::impl::PublisherPullCallbacks cbs{};
    if (state.has_connect_cb) {
        cbs.on_pull_request = state.connect_cb.on_pull_request;
        cbs.on_pull_release = state.connect_cb.on_pull_release;
        cbs.userdata = state.connect_cb.userdata;
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
        cbs);
}
#endif

}  // namespace rflow::service::internal

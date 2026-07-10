#ifndef __RFLOW_SERVICE_STREAM_STARTUP_POLICY_H__
#define __RFLOW_SERVICE_STREAM_STARTUP_POLICY_H__

#include "handles.h"
#include "state.h"

#include "rflow/Service/librflow_service_api.h"

#include <memory>
#include <optional>
#include <string>

namespace rflow::service::internal {

struct ResolvedPublisherStartup {
    int width{0};
    int height{0};
    int fps{30};
    int bitrate_kbps_target{0};
    int bitrate_kbps_min{0};
    int bitrate_kbps_max{0};
    std::string video_codec{"h264"};
    bool use_internal_video_source{false};
    std::string video_device_path;
    int video_device_index{0};
    std::string signal_url;
    std::string device_id;
    std::string stream_id;
    /* 由 stream_param 显式设置时非空，覆盖 SDK 默认 degradation_preference */
    std::optional<std::string> degradation_pref_explicit;
    std::optional<std::string> h264_profile;
    std::optional<std::string> h264_level;
    std::optional<int>         keyframe_gop;
    std::optional<bool>        ice_prioritize_likely_pairs;
    std::optional<std::string> stun_server;
    std::optional<std::string> turn_server;
    std::optional<std::string> turn_username;
    std::optional<std::string> turn_password;
    std::optional<std::string> video_network_priority;
    std::optional<int>         video_encoding_max_framerate;
    std::optional<int>         capture_warmup_sec;
    std::optional<int>         capture_gate_min_frames;
    std::optional<int>         capture_gate_max_wait_sec;
    std::optional<rflow_bitrate_mode_t> bitrate_mode;
};

ResolvedPublisherStartup ResolvePublisherStartup(const librflow_svc_stream_s& stream,
                                                const State& state,
                                                int32_t stream_idx);

#if defined(RFLOW_SVC_WEBRTC_IMPL)
std::shared_ptr<void> CreatePublisherImplForStream(const librflow_svc_stream_s& stream,
                                                   const State& state,
                                                   int32_t stream_idx);
#endif

}  // namespace rflow::service::internal

#endif  // __RFLOW_SERVICE_STREAM_STARTUP_POLICY_H__

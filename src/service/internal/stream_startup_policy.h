#ifndef __RFLOW_SERVICE_STREAM_STARTUP_POLICY_H__
#define __RFLOW_SERVICE_STREAM_STARTUP_POLICY_H__

#include "handles.h"
#include "state.h"

#include <memory>
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

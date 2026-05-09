#ifndef __RFLOW_SERVICE_RUNTIME_KNOBS_H__
#define __RFLOW_SERVICE_RUNTIME_KNOBS_H__

namespace rflow::service::internal {

struct RuntimeKnobs {
    int default_fps{30};
    int default_bitrate_kbps{0};
    int default_min_bitrate_kbps{0};
    int default_max_bitrate_kbps{0};
    bool prefer_internal_video_source{false};
};

const RuntimeKnobs& GetRuntimeKnobs();

}  // namespace rflow::service::internal

#endif  // __RFLOW_SERVICE_RUNTIME_KNOBS_H__

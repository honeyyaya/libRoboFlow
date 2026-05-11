#ifndef RFLOW_SERVICE_DEFAULT_PARAMS_H
#define RFLOW_SERVICE_DEFAULT_PARAMS_H

namespace rflow::service::internal {

struct ServiceDefaultParams {
    int default_fps{30};
    int default_bitrate_kbps{0};
    int default_min_bitrate_kbps{0};
    int default_max_bitrate_kbps{0};
    bool prefer_internal_video_source{false};
};

const ServiceDefaultParams& GetServiceDefaultParams();

}  // namespace rflow::service::internal

#endif  // RFLOW_SERVICE_DEFAULT_PARAMS_H

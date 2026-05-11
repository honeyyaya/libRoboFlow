#include "internal/service_default_params.h"

#include "runtime/runtime_knobs.h"

namespace rflow::service::internal {

const ServiceDefaultParams& GetServiceDefaultParams() {
    // 字段从 core/runtime 集中注册表读；结构与 Service API 默认值策略对齐。
    static const ServiceDefaultParams params = []() {
        namespace knob = rflow::core::runtime;
        ServiceDefaultParams k;
        k.default_fps                  = knob::ReadInt("RFLOW_SVC_DEFAULT_FPS");
        k.default_bitrate_kbps        = knob::ReadInt("RFLOW_SVC_DEFAULT_BITRATE_KBPS");
        k.default_min_bitrate_kbps    = knob::ReadInt("RFLOW_SVC_DEFAULT_MIN_BITRATE_KBPS");
        k.default_max_bitrate_kbps    = knob::ReadInt("RFLOW_SVC_DEFAULT_MAX_BITRATE_KBPS");
        k.prefer_internal_video_source =
            knob::ReadBool("RFLOW_SVC_PREFER_INTERNAL_VIDEO_SOURCE");
        return k;
    }();
    return params;
}

}  // namespace rflow::service::internal

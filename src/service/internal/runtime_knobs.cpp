#include "service/internal/runtime_knobs.h"

#include "core/runtime/runtime_knobs.h"

namespace rflow::service::internal {

const RuntimeKnobs& GetRuntimeKnobs() {
    // 业务字段从 core/runtime 集中注册表读，保留 RuntimeKnobs 结构体形态供调用方复用。
    static const RuntimeKnobs knobs = []() {
        namespace knob = rflow::core::runtime;
        RuntimeKnobs k;
        k.default_fps              = knob::ReadInt("RFLOW_SVC_DEFAULT_FPS");
        k.default_bitrate_kbps     = knob::ReadInt("RFLOW_SVC_DEFAULT_BITRATE_KBPS");
        k.default_min_bitrate_kbps = knob::ReadInt("RFLOW_SVC_DEFAULT_MIN_BITRATE_KBPS");
        k.default_max_bitrate_kbps = knob::ReadInt("RFLOW_SVC_DEFAULT_MAX_BITRATE_KBPS");
        k.prefer_internal_video_source =
            knob::ReadBool("RFLOW_SVC_PREFER_INTERNAL_VIDEO_SOURCE");
        return k;
    }();
    return knobs;
}

}  // namespace rflow::service::internal

#ifndef __RFLOW_CORE_HW_REGISTRY_BACKEND_ID_H__
#define __RFLOW_CORE_HW_REGISTRY_BACKEND_ID_H__

#include <cstdint>

namespace rflow::hw {

enum class HwBackendId : uint16_t {
  kBuiltin = 0,
  kRockchipMpp = 1,
  kAndroidMediaCodec = 2,
  kNvidiaNvCodec = 3,
};

inline const char* BackendName(HwBackendId id) {
  switch (id) {
    case HwBackendId::kRockchipMpp: return "rockchip_mpp";
    case HwBackendId::kAndroidMediaCodec: return "android_mediacodec";
    case HwBackendId::kNvidiaNvCodec: return "nvidia_nvcodec";
    case HwBackendId::kBuiltin:
    default: return "builtin";
  }
}

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_REGISTRY_BACKEND_ID_H__

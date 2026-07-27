#ifndef __RFLOW_CORE_HW_TYPES_PIXEL_FORMAT_H__
#define __RFLOW_CORE_HW_TYPES_PIXEL_FORMAT_H__

#include <cstdint>

namespace rflow::hw {

enum class HwPixelFormat : uint8_t {
  kUnknown = 0,
  kI420 = 1,
  kNv12 = 2,
  kNv21 = 3,
  kMjpeg = 4,
  kNativeVendor = 255,
};

inline uint32_t PlaneCount(HwPixelFormat fmt) {
  switch (fmt) {
    case HwPixelFormat::kI420: return 3;
    case HwPixelFormat::kNv12:
    case HwPixelFormat::kNv21: return 2;
    default: return 0;
  }
}

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_TYPES_PIXEL_FORMAT_H__

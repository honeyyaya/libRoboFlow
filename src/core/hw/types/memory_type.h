#ifndef __RFLOW_CORE_HW_TYPES_MEMORY_TYPE_H__
#define __RFLOW_CORE_HW_TYPES_MEMORY_TYPE_H__

#include <cstdint>

namespace rflow::hw {

enum class HwMemoryType : uint8_t {
  kCpuHost = 0,
  kDmabufFd = 1,
  kCudaDevice = 2,
  kAndroidHardwareBuffer = 3,
  kOpaqueNative = 4,
};

enum class HwNativeHandleTag : uint16_t {
  kNone = 0,
  kRockchipMppBuffer = 1,
  kRockchipMppFrame = 2,
  kAndroidHardwareBuffer = 3,
  kCudaDevicePointer = 4,
};

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_TYPES_MEMORY_TYPE_H__

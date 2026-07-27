#ifndef __RFLOW_CORE_HW_TYPES_ERROR_H__
#define __RFLOW_CORE_HW_TYPES_ERROR_H__

#include <cstdint>

namespace rflow::hw {

enum class HwCodecError : int32_t {
  kOk = 0,
  kInvalidArgument = -1,
  kNotInitialized = -2,
  kNotSupported = -3,
  kOutOfMemory = -4,
  kTimeout = -5,
  kHardwareFailure = -6,
  kAgain = -7,
};

inline bool Ok(HwCodecError e) { return e == HwCodecError::kOk; }

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_TYPES_ERROR_H__

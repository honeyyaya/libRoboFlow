#ifndef __RFLOW_COMMON_ABI_HANDLE_H__
#define __RFLOW_COMMON_ABI_HANDLE_H__

#include <atomic>
#include <cstdint>

#include "rflow/librflow_common.h"

namespace rflow {

constexpr uint32_t kMagicLogConfig = 0x52624C47;      // 'RbLG'
constexpr uint32_t kMagicSignalConfig = 0x52625347;   // 'RbSG'
constexpr uint32_t kMagicLicenseConfig = 0x52624C43;  // 'RbLC'
constexpr uint32_t kMagicGlobalConfig = 0x52624743;   // 'RbGC'
constexpr uint32_t kMagicVideoFrame = 0x52625646;     // 'RbVF'
constexpr uint32_t kMagicStreamStats = 0x52625354;    // 'RbST'

#define RFLOW_CHECK_HANDLE(ptr, expected_magic)                   \
    do {                                                          \
        if ((ptr) == nullptr || (ptr)->magic != (expected_magic)) \
            return RFLOW_ERR_PARAM;                               \
    } while (0)

}  // namespace rflow

#endif  // __RFLOW_COMMON_ABI_HANDLE_H__

/**
 * @file   handle.h
 * @brief  opaque handle 定义的公共基础设施
 *
 * 所有对外 opaque 类型在实现层通过此模板定义真实结构体。每个结构体都带
 * 一个 magic 字段，便于调试时检测野指针。
**/

#ifndef __ROBRT_INTERNAL_HANDLE_H__
#define __ROBRT_INTERNAL_HANDLE_H__

#include <atomic>
#include <cstdint>

#include "robrt/librobrt_common.h"

namespace robrt {

constexpr uint32_t kMagicLogConfig     = 0x52624C47;  // 'RbLG'
constexpr uint32_t kMagicSignalConfig  = 0x52625347;  // 'RbSG'
constexpr uint32_t kMagicLicenseConfig = 0x52624C43;  // 'RbLC'
constexpr uint32_t kMagicGlobalConfig  = 0x52624743;  // 'RbGC'
constexpr uint32_t kMagicVideoFrame    = 0x52625646;  // 'RbVF'
constexpr uint32_t kMagicAudioFrame    = 0x52624146;  // 'RbAF'
constexpr uint32_t kMagicStreamStats   = 0x52625354;  // 'RbST'

#define ROBRT_CHECK_HANDLE(ptr, expected_magic)                  \
    do {                                                         \
        if ((ptr) == nullptr || (ptr)->magic != (expected_magic)) \
            return ROBRT_ERR_PARAM;                              \
    } while (0)

}  // namespace robrt

#endif  // __ROBRT_INTERNAL_HANDLE_H__

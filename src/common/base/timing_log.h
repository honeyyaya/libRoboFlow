/**
 * @file timing_log.h
 * @brief 耗时 / 流水线日志开关（Client Pipeline、Android MediaCodec [Timing/Decode] 等共用）。
 *
 * 默认关闭。启用：RFLOW_LOG_TIMING=1；Android 另支持 debug.rflow.log_timing 属性。
 */
#ifndef __RFLOW_COMMON_BASE_TIMING_LOG_H__
#define __RFLOW_COMMON_BASE_TIMING_LOG_H__

#include <cstdint>
#include <cstdlib>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace rflow::timing_log {

namespace detail {

inline bool IsTruthyTimingToken(const char* v) {
    if (!v || !v[0]) return false;
    const char c = v[0];
    return c == '1' || c == 'y' || c == 'Y' || c == 't' || c == 'T' || c == 'o' || c == 'O';
}

inline bool ReadTimingSwitchOnce() {
    if (IsTruthyTimingToken(std::getenv("RFLOW_LOG_TIMING"))) {
        return true;
    }
#if defined(__ANDROID__)
    char prop_buf[PROP_VALUE_MAX] = {};
    if (__system_property_get("debug.rflow.log_timing", prop_buf) > 0) {
        return IsTruthyTimingToken(prop_buf);
    }
#endif
    return false;
}

}  // namespace detail

inline bool IsEnabled() {
    static const bool enabled = detail::ReadTimingSwitchOnce();
    return enabled;
}

inline bool ShouldSampleByTrackingId(uint32_t tracking_id, uint32_t period = 120u) {
    if (!IsEnabled() || period == 0u) return false;
    return (tracking_id % period) == 0u;
}

}  // namespace rflow::timing_log

#endif  // __RFLOW_COMMON_BASE_TIMING_LOG_H__

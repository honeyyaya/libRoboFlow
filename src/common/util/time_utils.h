#ifndef __RFLOW_COMMON_UTIL_TIME_UTILS_H__
#define __RFLOW_COMMON_UTIL_TIME_UTILS_H__

#include <chrono>
#include <cstdint>

namespace rflow::common::base {

inline int64_t MonoTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline int64_t MonoTimeUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline int64_t SignalingNowUs() {
    return MonoTimeUs();
}

}  // namespace rflow::common::base

namespace rflow::common::util {
using rflow::common::base::MonoTimeMs;
using rflow::common::base::MonoTimeUs;
using rflow::common::base::SignalingNowUs;
}  // namespace rflow::common::util

#endif  // __RFLOW_COMMON_UTIL_TIME_UTILS_H__

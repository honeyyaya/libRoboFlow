#ifndef __RFLOW_COMMON_TRACE_TRACE_FLAGS_H__
#define __RFLOW_COMMON_TRACE_TRACE_FLAGS_H__

#include "util/env_reader.h"

namespace rflow::common::base {

inline bool TraceFlagEnabled(const char* env_name) {
    return ReadEnvBool(env_name, false);
}

inline bool LatencyTraceEnabled() {
    static const bool enabled = TraceFlagEnabled("RFLOW_LATENCY_TRACE");
    return enabled;
}

inline bool MediaTimingTraceEnabled() {
    static const bool enabled = TraceFlagEnabled("RFLOW_MEDIA_TIMING_TRACE");
    return enabled;
}

inline unsigned MediaTimingTraceEveryN() {
    static const unsigned every_n = static_cast<unsigned>(
        ReadEnvIntInRange("RFLOW_MEDIA_TIMING_TRACE_EVERY_N", 30, 1, 600));
    return every_n;
}

inline bool SignalingTimingTraceEnabled() {
    static const bool enabled = TraceFlagEnabled("RFLOW_SIGNALING_TIMING_TRACE");
    return enabled;
}

}  // namespace rflow::common::base

namespace rflow::common::util {
using rflow::common::base::LatencyTraceEnabled;
using rflow::common::base::MediaTimingTraceEnabled;
using rflow::common::base::MediaTimingTraceEveryN;
using rflow::common::base::SignalingTimingTraceEnabled;
using rflow::common::base::TraceFlagEnabled;
}  // namespace rflow::common::util

#endif  // __RFLOW_COMMON_TRACE_TRACE_FLAGS_H__

#ifndef __RFLOW_COMMON_TRACE_SIGNALING_TRACE_H__
#define __RFLOW_COMMON_TRACE_SIGNALING_TRACE_H__

#include "public/log_tagged.h"
#include "trace/trace_flags.h"
#include "util/time_utils.h"

#include <cstdint>
#include <string>

namespace rflow::common::base {

inline void TraceSigTiming(const char* role, const std::string& msg) {
    if (!SignalingTimingTraceEnabled()) {
        return;
    }
    RFLOW_LOG_TAG_I("SIG_TIMING", "[%s] t_us=%lld %s", role,
                    static_cast<long long>(SignalingNowUs()), msg.c_str());
}

inline void TraceSigTimingPlain(const std::string& msg) {
    if (!SignalingTimingTraceEnabled()) {
        return;
    }
    RFLOW_LOGI("[SIG_TIMING] t_us=%lld %s", static_cast<long long>(SignalingNowUs()), msg.c_str());
}

}  // namespace rflow::common::base

#endif  // __RFLOW_COMMON_TRACE_SIGNALING_TRACE_H__

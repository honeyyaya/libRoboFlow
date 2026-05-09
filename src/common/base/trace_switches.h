#ifndef __RFLOW_COMMON_BASE_TRACE_SWITCHES_H__
#define __RFLOW_COMMON_BASE_TRACE_SWITCHES_H__

#include "common/base/env_reader.h"

namespace rflow::common::base {

inline bool TraceFlagEnabled(const char* env_name) {
    return ReadEnvBool(env_name, false);
}

}  // namespace rflow::common::base

namespace rflow::common::util {
using rflow::common::base::TraceFlagEnabled;
}  // namespace rflow::common::util

#endif  // __RFLOW_COMMON_BASE_TRACE_SWITCHES_H__

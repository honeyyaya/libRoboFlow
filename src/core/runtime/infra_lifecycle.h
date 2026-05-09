#ifndef __RFLOW_CORE_RUNTIME_INFRA_LIFECYCLE_H__
#define __RFLOW_CORE_RUNTIME_INFRA_LIFECYCLE_H__

#include "rflow/librflow_common.h"

namespace rflow::core::runtime {

enum class InitFailureStage {
    kNone = 0,
    kThread = 1,
    kRtc = 2,
    kSignal = 3,
};

rflow_err_t InitInfrastructure(InitFailureStage* out_failure_stage = nullptr);
void ShutdownInfrastructure();

}  // namespace rflow::core::runtime

#endif  // __RFLOW_CORE_RUNTIME_INFRA_LIFECYCLE_H__

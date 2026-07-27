#ifndef __RFLOW_COMMON_STATE_LIFECYCLE_FSM_H__
#define __RFLOW_COMMON_STATE_LIFECYCLE_FSM_H__

#include "rflow/librflow_common.h"

namespace rflow::common::base {

template <typename LifecycleStateT>
inline rflow_err_t ValidateConnectTransition(LifecycleStateT state,
                                             LifecycleStateT uninit_state,
                                             LifecycleStateT connecting_state,
                                             LifecycleStateT connected_state) {
    if (state == uninit_state) return RFLOW_ERR_STATE;
    if (state == connecting_state || state == connected_state) return RFLOW_ERR_STATE;
    return RFLOW_OK;
}

}  // namespace rflow::common::base

#endif  // __RFLOW_COMMON_STATE_LIFECYCLE_FSM_H__

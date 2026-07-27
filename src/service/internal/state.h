#ifndef __RFLOW_SERVICE_STATE_H__
#define __RFLOW_SERVICE_STATE_H__

#include "base/sdk_state.h"
#include "handles.h"

namespace rflow::service {

using LifecycleState = rflow::common::base::SdkLifecycleState;

using State = rflow::common::base::SdkStateBase<librflow_svc_connect_info_s,
                                                librflow_svc_connect_cb_s,
                                                librflow_svc_stream_handle_t,
                                                librflow_svc_stream_s>;

State& state();

}  // namespace rflow::service

#endif  // __RFLOW_SERVICE_STATE_H__

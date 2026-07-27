#ifndef __RFLOW_CLIENT_STATE_H__
#define __RFLOW_CLIENT_STATE_H__

#include "base/sdk_state.h"
#include "handles.h"

namespace rflow::client {

using LifecycleState = rflow::common::base::SdkLifecycleState;

using State = rflow::common::base::SdkStateBase<librflow_connect_info_s,
                                                librflow_connect_cb_s,
                                                librflow_stream_handle_t,
                                                librflow_stream_s>;

State& state();

}  // namespace rflow::client

#endif  // __RFLOW_CLIENT_STATE_H__

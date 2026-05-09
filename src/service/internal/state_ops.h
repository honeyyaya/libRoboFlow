#ifndef __RFLOW_SERVICE_STATE_OPS_H__
#define __RFLOW_SERVICE_STATE_OPS_H__

#include "handles.h"
#include "state.h"

#include <memory>

namespace rflow::service::internal {

std::shared_ptr<librflow_svc_stream_s> FindStreamByHandleLocked(State& state,
                                                                 librflow_svc_stream_handle_t handle);
void MarkStreamStarted(const std::shared_ptr<librflow_svc_stream_s>& stream,
                       librflow_svc_stream_handle_t handle);
void MarkStreamStopped(const std::shared_ptr<librflow_svc_stream_s>& stream,
                       librflow_svc_stream_handle_t handle);
void MarkStreamDestroyed(const std::shared_ptr<librflow_svc_stream_s>& stream,
                         librflow_svc_stream_handle_t handle);

rflow_err_t InitSubsystems();
void ShutdownSubsystems();
rflow_err_t ConnectStateTransition(State& state,
                                   const librflow_svc_connect_info_s& info,
                                   const librflow_svc_connect_cb_s& cb);
rflow_err_t DisconnectStateTransition(State& state);

}  // namespace rflow::service::internal

#endif  // __RFLOW_SERVICE_STATE_OPS_H__

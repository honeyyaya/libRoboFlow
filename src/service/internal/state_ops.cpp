#include "internal/state_ops.h"

#include "base/lifecycle_fsm.h"
#include "base/stream_handle_ops.h"
#include "runtime/infra_lifecycle.h"

namespace rflow::service::internal {

std::shared_ptr<librflow_svc_stream_s> FindStreamByHandleLocked(State& state,
                                                                 librflow_svc_stream_handle_t handle) {
    return rflow::common::base::LookupSharedFromMap(state.streams, handle);
}

void MarkStreamStarted(const std::shared_ptr<librflow_svc_stream_s>& stream,
                       librflow_svc_stream_handle_t handle) {
    stream->started = true;
    stream->started_at = std::chrono::steady_clock::now();
    rflow::common::base::EmitStreamStateChange(*stream, handle, RFLOW_STREAM_OPENED, RFLOW_OK);
}

void MarkStreamStopped(const std::shared_ptr<librflow_svc_stream_s>& stream,
                       librflow_svc_stream_handle_t handle) {
    stream->started = false;
    stream->started_at = {};
    rflow::common::base::EmitStreamStateChange(*stream, handle, RFLOW_STREAM_IDLE, RFLOW_OK);
}

void MarkStreamDestroyed(const std::shared_ptr<librflow_svc_stream_s>& stream,
                         librflow_svc_stream_handle_t handle) {
    stream->started = false;
    stream->started_at = {};
    rflow::common::base::EmitStreamStateChange(*stream, handle, RFLOW_STREAM_CLOSED, RFLOW_OK);
    stream->magic = 0;
}

rflow_err_t InitSubsystems() {
    return rflow::core::runtime::InitInfrastructure();
}

void ShutdownSubsystems() {
    rflow::core::runtime::ShutdownInfrastructure();
}

rflow_err_t ConnectStateTransition(State& state,
                                   const librflow_svc_connect_info_s& info,
                                   const librflow_svc_connect_cb_s& cb) {
    const auto rc = rflow::common::base::ValidateConnectTransition(state.lifecycle,
                                                                    LifecycleState::kUninit,
                                                                    LifecycleState::kConnecting,
                                                                    LifecycleState::kConnected);
    if (rc != RFLOW_OK) return rc;

    state.connect_info = info;
    if (state.connect_info.device_id.empty()) {
        state.connect_info.device_id = RFLOW_DEFAULT_DEVICE_ID;
    }
    state.connect_cb = cb;
    state.has_connect_cb = true;
    state.lifecycle = LifecycleState::kConnecting;
    state.lifecycle = LifecycleState::kConnected;
    if (state.connect_cb.on_state) {
        state.connect_cb.on_state(RFLOW_CONN_CONNECTED, RFLOW_OK, state.connect_cb.userdata);
    }
    if (state.connect_cb.on_bind_state) {
        state.connect_cb.on_bind_state(RFLOW_BIND_BOUND, nullptr, state.connect_cb.userdata);
    }
    return RFLOW_OK;
}

rflow_err_t DisconnectStateTransition(State& state) {
    if (state.lifecycle != LifecycleState::kConnected &&
        state.lifecycle != LifecycleState::kConnecting) {
        return RFLOW_OK;
    }
    rflow::common::base::BroadcastStreamStateToAllCb(state.streams,
                                                     RFLOW_STREAM_CLOSED,
                                                     RFLOW_OK);
    state.streams.clear();
    if (state.has_connect_cb && state.connect_cb.on_state) {
        state.connect_cb.on_state(RFLOW_CONN_DISCONNECTED, RFLOW_OK, state.connect_cb.userdata);
    }
    state.has_connect_cb = false;
    state.lifecycle = LifecycleState::kInited;
    return RFLOW_OK;
}

}  // namespace rflow::service::internal

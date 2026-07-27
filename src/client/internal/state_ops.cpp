#include "internal/state_ops.h"

#include "base/lifecycle_fsm.h"
#include "base/stream_handle_ops.h"
#include "rflow/librflow_common.h"

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
#include "impl/rtc_stream/rtc_stream_manager.h"
#include "impl/rtc_stream/rtc_stream_session.h"
#endif

namespace rflow::client::internal {

std::shared_ptr<librflow_stream_s> FindStreamByHandleLocked(State& state,
                                                            librflow_stream_handle_t handle) {
    return rflow::common::base::FindStreamByHandleLocked(state, handle);
}

void MarkStreamClosed(const std::shared_ptr<librflow_stream_s>& stream,
                      librflow_stream_handle_t handle) {
    if (!stream) {
        return;
    }
    rflow::common::base::MarkStreamClosed(*stream, handle);
}

rflow_err_t ValidateConnectTransitionLocked(const State& state) {
    return rflow::common::base::ValidateConnectTransition(state.lifecycle,
                                                          LifecycleState::kUninit,
                                                          LifecycleState::kConnecting,
                                                          LifecycleState::kConnected);
}

void ApplyConnectTransitionLocked(State& state,
                                  const librflow_connect_info_s& info,
                                  const librflow_connect_cb_s& cb,
                                  std::string* signal_url,
                                  std::string* device_id,
                                  librflow_connect_cb_s* cb_copy) {
    state.connect_info = info;
    state.connect_cb = cb;
    state.has_connect_cb = true;
    state.lifecycle = LifecycleState::kConnecting;
    if (state.connect_info.device_id.empty()) {
        state.connect_info.device_id = RFLOW_DEFAULT_DEVICE_ID;
    }
    *device_id = state.connect_info.device_id;
    if (state.global_config.magic == rflow::kMagicGlobalConfig && state.global_config.has_signal) {
        *signal_url = state.global_config.signal.url;
    } else {
        signal_url->clear();
    }
    *cb_copy = state.connect_cb;
}

void RollbackConnectFailedLocked(State& state) {
    state.lifecycle = LifecycleState::kInited;
    state.has_connect_cb = false;
}

void CommitConnectedLocked(State& state) {
    state.lifecycle = LifecycleState::kConnected;
}

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
rflow_err_t OpenRtcStreamSession(int32_t index,
                                 librflow_stream_param_t param,
                                 StreamStateSink state_sink,
                                 StreamFrameSink frame_sink,
                                 std::shared_ptr<void>* out_impl) {
    if (!out_impl) return RFLOW_ERR_PARAM;
    std::shared_ptr<rflow::client::impl::RtcStreamSession> pull;
    const rflow_err_t err = rflow::client::impl::RtcStreamManager::Instance().OpenStream(
        index, param, std::move(state_sink), std::move(frame_sink), &pull);
    if (err != RFLOW_OK) return err;
    *out_impl = std::static_pointer_cast<void>(pull);
    return RFLOW_OK;
}

void CloseRtcStreamSession(std::shared_ptr<void> impl) {
    if (!impl) return;
    auto pull = std::static_pointer_cast<rflow::client::impl::RtcStreamSession>(std::move(impl));
    pull->Close();
}

bool CollectRtcStreamStats(const std::shared_ptr<void>& impl, librflow_stream_stats_s* stats) {
    if (!impl || !stats) return false;
    auto pull = std::static_pointer_cast<rflow::client::impl::RtcStreamSession>(impl);
    return pull->CollectStats(stats);
}
#endif

}  // namespace rflow::client::internal

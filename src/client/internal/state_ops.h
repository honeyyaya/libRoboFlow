#ifndef __RFLOW_CLIENT_STATE_OPS_H__
#define __RFLOW_CLIENT_STATE_OPS_H__

#include "handles.h"
#include "state.h"

#include <functional>
#include <memory>
#include <string>

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
namespace webrtc {
class VideoFrame;
}
#endif

namespace rflow::client::internal {

std::shared_ptr<librflow_stream_s> FindStreamByHandleLocked(State& state,
                                                            librflow_stream_handle_t handle);

void MarkStreamClosed(const std::shared_ptr<librflow_stream_s>& stream,
                      librflow_stream_handle_t handle);

rflow_err_t ValidateConnectTransitionLocked(const State& state);
void ApplyConnectTransitionLocked(State& state,
                                  const librflow_connect_info_s& info,
                                  const librflow_connect_cb_s& cb,
                                  std::string* signal_url,
                                  std::string* device_id,
                                  librflow_connect_cb_s* cb_copy);
void RollbackConnectFailedLocked(State& state);
void CommitConnectedLocked(State& state);

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
using StreamStateSink = std::function<void(rflow_stream_state_t, rflow_err_t)>;
using StreamFrameSink = std::function<void(const webrtc::VideoFrame&)>;

rflow_err_t OpenRtcStreamSession(int32_t index,
                                 librflow_stream_param_t param,
                                 StreamStateSink state_sink,
                                 StreamFrameSink frame_sink,
                                 std::shared_ptr<void>* out_impl);
void CloseRtcStreamSession(std::shared_ptr<void> impl);
bool CollectRtcStreamStats(const std::shared_ptr<void>& impl, librflow_stream_stats_s* stats);
#endif

}  // namespace rflow::client::internal

#endif  // __RFLOW_CLIENT_STATE_OPS_H__

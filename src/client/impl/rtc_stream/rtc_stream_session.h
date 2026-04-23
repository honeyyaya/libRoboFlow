/**
 * @file   rtc_stream_session.h
 * @brief  Single rtc stream session for client pull-side playback
 *
 * 改动要点：
 *   - 去除 Qt（QObject / QTimer / QMetaObject / QJniObject 等）；
 *   - 去除渲染：不再持有 VideoRenderer，也不向外发送 VideoTrack；
 *     远端视频帧通过 VideoSinkInterface 接入 FrameSink（std::function）；
 *   - 每路流 = 1 PeerConnection + 1 SignalingClient 会话，由管理类按 index 聚合；
 *   - stats 暂不实现（TODO）。
 */

#ifndef __RFLOW_CLIENT_IMPL_RTC_STREAM_SESSION_H__
#define __RFLOW_CLIENT_IMPL_RTC_STREAM_SESSION_H__

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "rflow/librflow_common.h"
#include "core/signal/session.h"

#include "api/jsep.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "api/set_remote_description_observer_interface.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

namespace rflow::client::impl {

class RtcStreamSession : public std::enable_shared_from_this<RtcStreamSession>,
                         private rflow::signal::SessionDelegate {
 public:
    using FrameSink = std::function<void(const webrtc::VideoFrame& frame)>;
    using StateSink = std::function<void(rflow_stream_state_t state, rflow_err_t reason)>;

    RtcStreamSession(int32_t index,
                     webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory,
                     std::string signaling_url,
                     std::string device_id);
    ~RtcStreamSession();

    RtcStreamSession(const RtcStreamSession&)            = delete;
    RtcStreamSession& operator=(const RtcStreamSession&) = delete;

    int32_t index() const { return index_; }

    void SetFrameSink(FrameSink sink);
    void SetStateSink(StateSink sink);

    // 启动信令并等待 Offer；成功仅表示「进入 OPENING」。
    bool Start();
    // 幂等强清理。
    void Close();

 private:
    class PeerConnectionObserverImpl;
    class FrameAdapter;
    friend class PeerConnectionObserverImpl;

    void OnSignalMessage(const rflow::signal::Message& msg) override;
    void OnSignalError(std::string_view error) override;

    void HandleOffer(const std::string& sdp);
    void HandleRemoteIceCandidate(const std::string& mid, int mline_index,
                                  const std::string& candidate);

    void CreatePeerConnectionLocked();
    void DoCreateAnswerAfterSetRemote();
    void AddRemoteIceCandidateNow(const std::string& mid, int mline_index,
                                  const std::string& candidate);
    void FlushPendingRemoteIceCandidates();

    void EmitState(rflow_stream_state_t state, rflow_err_t reason);

    const int32_t     index_;
    const std::string signaling_url_;
    const std::string device_id_;

    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;

    std::unique_ptr<rflow::signal::Session>             signaling_;
    std::unique_ptr<PeerConnectionObserverImpl>         observer_;
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
    std::unique_ptr<FrameAdapter>                       frame_adapter_;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface>  current_video_track_;

    // SetRemote / CreateAnswer / SetLocal 异步完成前持有 observer，避免回调前析构。
    webrtc::scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface> pending_set_remote_observer_;
    webrtc::scoped_refptr<webrtc::CreateSessionDescriptionObserver>       pending_create_answer_observer_;
    webrtc::scoped_refptr<webrtc::SetSessionDescriptionObserver>          pending_set_local_observer_;

    struct PendingRemoteIce {
        std::string mid;
        int         mline_index = 0;
        std::string candidate;
    };
    std::vector<PendingRemoteIce> pending_remote_ice_;
    bool                          remote_description_applied_ = false;

    std::atomic<int32_t> stream_state_{RFLOW_STREAM_IDLE};
    std::atomic<bool>    closed_{false};

    std::mutex mu_;
    FrameSink  frame_sink_;
    StateSink  state_sink_;
};

}  // namespace rflow::client::impl

#endif  // __RFLOW_CLIENT_IMPL_RTC_STREAM_SESSION_H__

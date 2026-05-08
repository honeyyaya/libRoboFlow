/**
 * @file   rtc_stream_session.h
 * @brief  Single rtc stream session for client pull-side playback
 */

#ifndef __RFLOW_CLIENT_IMPL_RTC_STREAM_SESSION_H__
#define __RFLOW_CLIENT_IMPL_RTC_STREAM_SESSION_H__

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rflow/librflow_common.h"
#include "core/signal/session.h"

#include "api/jsep.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "api/set_remote_description_observer_interface.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

struct librflow_stream_stats_s;

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

    RtcStreamSession(const RtcStreamSession&) = delete;
    RtcStreamSession& operator=(const RtcStreamSession&) = delete;

    int32_t index() const { return index_; }

    void SetFrameSink(FrameSink sink);
    void SetStateSink(StateSink sink);
    bool Start();
    void Close();
    bool CollectStats(librflow_stream_stats_s* out_stats);

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
    bool RunOnPeerConnectionSignalingThread(const std::function<void()>& task);
    void DoCreateAnswerAfterSetRemote();
    void AddRemoteIceCandidateNow(const std::string& mid, int mline_index,
                                  const std::string& candidate);
    void FlushPendingRemoteIceCandidates();
    void EmitState(rflow_stream_state_t state, rflow_err_t reason);

    void StartWatchdogThread();
    void StopWatchdogThread();
    void RunWatchdogLoop();
    void TickWatchdog();
    void OnWatchdogStats(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report);
    void KickJitterBufferForKeyframe();

    const int32_t index_;
    const std::string signaling_url_;
    const std::string device_id_;

    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;

    std::unique_ptr<rflow::signal::Session> signaling_;
    std::unique_ptr<PeerConnectionObserverImpl> observer_;
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
    std::unique_ptr<FrameAdapter> frame_adapter_;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> current_video_track_;

    webrtc::scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface> pending_set_remote_observer_;
    webrtc::scoped_refptr<webrtc::CreateSessionDescriptionObserver> pending_create_answer_observer_;
    webrtc::scoped_refptr<webrtc::SetSessionDescriptionObserver> pending_set_local_observer_;

    struct PendingRemoteIce {
        std::string mid;
        int mline_index = 0;
        std::string candidate;
    };
    std::vector<PendingRemoteIce> pending_remote_ice_;
    std::atomic<bool> remote_description_applied_{false};

    std::atomic<int32_t> stream_state_{RFLOW_STREAM_IDLE};
    std::atomic<bool> closed_{false};

    // 当前生效的视频 RtpReceiver jitter buffer min-delay 下限（秒）。OnTrack 写一次，
    // watchdog 触发后用于把临时 bump 上去的延迟回归到该 floor。
    std::atomic<double> jitter_min_delay_seconds_{0.02};

    // 关键帧 watchdog：1Hz 拉 GetStats，packetsReceived 仍在涨但 framesDecoded 停滞达
    // 阈值（默认 300ms）则触发一次 bump—back-to-floor 的 jitter min-delay 抖动，迫使
    // RtpVideoStreamReceiver 重置 PLI/NACK 定时器。env RFLOW_RECEIVER_KEYFRAME_WATCHDOG=0 关。
    std::thread             stats_thread_;
    std::atomic<bool>       stats_running_{false};
    std::condition_variable stats_cv_;
    std::mutex              stats_cv_mu_;
    bool                    watchdog_enabled_           = true;
    int64_t                 watchdog_stuck_threshold_ms_ = 300;
    int64_t                 watchdog_cooldown_ms_        = 600;

    uint64_t prev_frames_decoded_         = 0;
    uint64_t prev_packets_received_       = 0;
    int64_t  last_decode_progress_mono_ms_ = 0;
    int64_t  last_keyframe_kick_mono_ms_   = 0;
    uint64_t last_keyframe_kick_packets_   = 0;

    // 周期性 stats 日志（[Pipeline/Video] / [Pipeline/Latency] /
    // [Pipeline/Codec] / [Pipeline/Net]），1Hz，复用 watchdog 线程。
    // 默认关闭，env RFLOW_LOG_TIMING=1 显式开启（rflow::timing_log::IsEnabled）。
    bool        stats_log_baseline_done_     = false;
    uint64_t    prev_frames_dropped_         = 0;
    double      prev_total_decode_time_s_    = 0.0;
    double      prev_total_processing_delay_s_ = 0.0;
    double      prev_total_assembly_time_s_  = 0.0;
    // [Pipeline/Codec] 只在 fmtp 首次出现或变化时打印；缓存最近一次。
    std::string last_codec_fmtp_;
    std::string last_codec_mime_;
    uint32_t    last_codec_payload_type_     = 0;

    std::mutex mu_;
    FrameSink frame_sink_;
    StateSink state_sink_;
};

}  // namespace rflow::client::impl

#endif  // __RFLOW_CLIENT_IMPL_RTC_STREAM_SESSION_H__

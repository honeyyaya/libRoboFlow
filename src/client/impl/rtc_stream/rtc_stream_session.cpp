#include "rtc_stream_session.h"

#include "core/rtc/rtc.h"
#include "signaling/signaling_client.h"

#include "common/internal/frame_impl.h"
#include "common/internal/logger.h"
#include "common/internal/timing_log.h"
#include "rflow/librflow_common.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "api/stats/rtcstats_objects.h"

#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/set_remote_description_observer_interface.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/video_codecs/h264_profile_level_id.h"
#include "rtc_base/thread.h"

namespace rflow::client::impl {

namespace {

class SetRemoteDescObserver : public webrtc::SetRemoteDescriptionObserverInterface {
 public:
    explicit SetRemoteDescObserver(std::function<void(webrtc::RTCError)> on_done)
        : on_done_(std::move(on_done)) {}

    void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override {
        if (on_done_) on_done_(std::move(error));
    }

 private:
    std::function<void(webrtc::RTCError)> on_done_;
};

class CreateAnswerObserver : public webrtc::CreateSessionDescriptionObserver {
 public:
    CreateAnswerObserver(
        std::function<void(webrtc::RTCError, std::unique_ptr<webrtc::SessionDescriptionInterface>)> cb,
        std::function<void(webrtc::RTCError)> fail)
        : cb_(std::move(cb)), fail_(std::move(fail)) {}

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
        if (cb_) {
            cb_(webrtc::RTCError::OK(),
                std::unique_ptr<webrtc::SessionDescriptionInterface>(desc));
        }
    }

    void OnFailure(webrtc::RTCError error) override {
        if (fail_) fail_(std::move(error));
    }

 private:
    std::function<void(webrtc::RTCError, std::unique_ptr<webrtc::SessionDescriptionInterface>)> cb_;
    std::function<void(webrtc::RTCError)> fail_;
};

class SetLocalDescObserver : public webrtc::SetSessionDescriptionObserver {
 public:
    SetLocalDescObserver(std::function<void()> ok, std::function<void(webrtc::RTCError)> fail)
        : ok_(std::move(ok)), fail_(std::move(fail)) {}

    void OnSuccess() override {
        if (ok_) ok_();
    }

    void OnFailure(webrtc::RTCError error) override {
        if (fail_) fail_(std::move(error));
    }

 private:
    std::function<void()> ok_;
    std::function<void(webrtc::RTCError)> fail_;
};

class StatsCollectorCallback : public webrtc::RTCStatsCollectorCallback {
 public:
    explicit StatsCollectorCallback(
        std::function<void(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)> cb)
        : cb_(std::move(cb)) {}

    void OnStatsDelivered(
        const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override {
        if (cb_) cb_(report);
    }

 private:
    std::function<void(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)> cb_;
 };

// 接收侧 jitter buffer 最低延迟（s）。设 20ms 让单包 NACK 有时间补救；env 可覆盖。
constexpr double kDefaultReceiverVideoJitterBufferMinDelaySeconds = 0.02;

double ReadReceiverJitterMinDelaySeconds() {
    if (const char* v = std::getenv("RFLOW_RECEIVER_JITTER_MIN_DELAY_MS")) {
        if (v[0] != '\0') {
            const int ms = std::atoi(v);
            if (ms >= 0 && ms <= 1000) {
                return static_cast<double>(ms) / 1000.0;
            }
        }
    }
    return kDefaultReceiverVideoJitterBufferMinDelaySeconds;
}

bool IsLowLatencyH264Capability(const webrtc::RtpCodecCapability& codec) {
    if (codec.kind != webrtc::MediaType::VIDEO || codec.name != "H264") {
        return false;
    }
    const std::optional<webrtc::H264ProfileLevelId> profile =
        webrtc::ParseSdpForH264ProfileLevelId(codec.parameters);
    if (!profile.has_value()) return false;
    return profile->profile == webrtc::H264Profile::kProfileConstrainedBaseline ||
           profile->profile == webrtc::H264Profile::kProfileBaseline;
}

// 排序：constrained-baseline H264 > 其它 H264 > 其它 media codec；rtx 仅保留 apt 命中的。
// 与 LibwebRtcDemo BuildLowLatencyVideoCodecPreferences 等价；用于在 SetCodecPreferences
// 中把低延迟可硬解的 H264 配置推到 SDP 前列。
std::vector<webrtc::RtpCodecCapability> BuildLowLatencyVideoCodecPreferences(
    const webrtc::RtpCapabilities& capabilities) {
    std::vector<webrtc::RtpCodecCapability> preferred_h264;
    std::vector<webrtc::RtpCodecCapability> other_h264;
    std::vector<webrtc::RtpCodecCapability> other_media;
    std::vector<webrtc::RtpCodecCapability> auxiliary;
    std::vector<int>                        allowed_pts;

    for (const auto& codec : capabilities.codecs) {
        if (codec.kind != webrtc::MediaType::VIDEO) continue;
        if (codec.IsMediaCodec()) {
            if (IsLowLatencyH264Capability(codec)) {
                preferred_h264.push_back(codec);
            } else if (codec.name == "H264") {
                other_h264.push_back(codec);
            } else {
                other_media.push_back(codec);
            }
            continue;
        }
        auxiliary.push_back(codec);
    }

    if (preferred_h264.empty() && other_h264.empty()) {
        return {};
    }

    auto collect_pt = [&allowed_pts](const std::vector<webrtc::RtpCodecCapability>& list) {
        for (const auto& c : list) {
            if (c.preferred_payload_type.has_value()) {
                allowed_pts.push_back(*c.preferred_payload_type);
            }
        }
    };
    collect_pt(preferred_h264);
    collect_pt(other_h264);
    collect_pt(other_media);

    std::vector<webrtc::RtpCodecCapability> result = preferred_h264;
    result.insert(result.end(), other_h264.begin(),  other_h264.end());
    result.insert(result.end(), other_media.begin(), other_media.end());
    for (const auto& codec : auxiliary) {
        if (codec.name == "rtx") {
            const auto apt_it = codec.parameters.find("apt");
            if (apt_it == codec.parameters.end()) continue;
            const int apt = std::atoi(apt_it->second.c_str());
            if (std::find(allowed_pts.begin(), allowed_pts.end(), apt) == allowed_pts.end()) {
                continue;
            }
        }
        result.push_back(codec);
    }
    return result;
}

}  // namespace

class RtcStreamSession::FrameAdapter final
    : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
    explicit FrameAdapter(RtcStreamSession* owner) : owner_(owner) {}

    void OnFrame(const webrtc::VideoFrame& frame) override {
        FrameSink sink;
        {
            std::lock_guard<std::mutex> lk(owner_->mu_);
            sink = owner_->frame_sink_;
        }
        if (sink) sink(frame);
    }

 private:
    RtcStreamSession* owner_;
};

class RtcStreamSession::PeerConnectionObserverImpl : public webrtc::PeerConnectionObserver {
 public:
    explicit PeerConnectionObserverImpl(RtcStreamSession* s) : stream_(s) {}

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}

    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState s) override {
        RFLOW_LOGD("[pull idx=%d] IceGatheringState=%d", stream_->index_, static_cast<int>(s));
    }

    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override {
        if (!candidate || !stream_->signaling_) return;

        std::string sdp;
        if (!candidate->ToString(&sdp)) return;

        rflow::signal::Message msg;
        msg.type = rflow::signal::MessageType::kIce;
        msg.mid = candidate->sdp_mid();
        msg.mline_index = candidate->sdp_mline_index();
        msg.candidate = std::move(sdp);
        if (!stream_->signaling_->Send(msg)) {
            RFLOW_LOGE("[pull idx=%d] send local ice candidate failed", stream_->index_);
            stream_->EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_CONN_NETWORK);
        }
    }

    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState s) override {
        using PCS = webrtc::PeerConnectionInterface::PeerConnectionState;
        if (s == PCS::kConnected) {
            stream_->EmitState(RFLOW_STREAM_OPENED, RFLOW_OK);
        } else if (s == PCS::kFailed) {
            stream_->EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_CONN_FAIL);
        } else if (s == PCS::kDisconnected || s == PCS::kClosed) {
            stream_->EmitState(RFLOW_STREAM_CLOSED, RFLOW_OK);
        }
    }

    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState s) override {
        if (s == webrtc::PeerConnectionInterface::kIceConnectionFailed) {
            RFLOW_LOGW("[pull idx=%d] ICE connection failed", stream_->index_);
        }
    }

    void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override {
        if (!transceiver || transceiver->media_type() != webrtc::MediaType::VIDEO) return;

        auto receiver = transceiver->receiver();
        if (!receiver) return;
        const double floor_s = ReadReceiverJitterMinDelaySeconds();
        receiver->SetJitterBufferMinimumDelay(std::optional<double>(floor_s));
        stream_->jitter_min_delay_seconds_ = floor_s;
        RFLOW_LOGI("[pull idx=%d] video jitter min delay floor = %.1f ms",
                   stream_->index_, floor_s * 1000.0);

        auto track = receiver->track();
        if (!track ||
            track->kind() != std::string(webrtc::MediaStreamTrackInterface::kVideoKind)) {
            return;
        }

        auto* vptr = static_cast<webrtc::VideoTrackInterface*>(track.get());
        webrtc::scoped_refptr<webrtc::VideoTrackInterface> v(vptr);

        {
            std::lock_guard<std::mutex> lk(stream_->mu_);
            stream_->current_video_track_ = v;
        }
        if (stream_->frame_adapter_) {
            webrtc::VideoSinkWants wants;
            v->AddOrUpdateSink(stream_->frame_adapter_.get(), wants);
        }
    }

 private:
    RtcStreamSession* stream_;
};

namespace {

bool ReadEnvBoolDefault(const char* name, bool def) {
    const char* v = std::getenv(name);
    if (!v || !v[0]) return def;
    if (v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F') return false;
    if (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T') return true;
    return def;
}

int64_t ReadEnvIntInRange(const char* name, int64_t def, int64_t lo, int64_t hi) {
    const char* v = std::getenv(name);
    if (!v || !v[0]) return def;
    const int64_t n = std::atoll(v);
    if (n < lo || n > hi) return def;
    return n;
}

int64_t MonoTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

RtcStreamSession::RtcStreamSession(
    int32_t index,
    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory,
    std::string signaling_url,
    std::string device_id)
    : index_(index),
      signaling_url_(std::move(signaling_url)),
      device_id_(std::move(device_id)),
      factory_(std::move(factory)) {
    observer_ = std::make_unique<PeerConnectionObserverImpl>(this);
    frame_adapter_ = std::make_unique<FrameAdapter>(this);
    watchdog_enabled_ = ReadEnvBoolDefault("RFLOW_RECEIVER_KEYFRAME_WATCHDOG", true);
    watchdog_stuck_threshold_ms_ =
        ReadEnvIntInRange("RFLOW_RECEIVER_WATCHDOG_STUCK_MS", 300, 50, 5000);
    watchdog_cooldown_ms_ =
        ReadEnvIntInRange("RFLOW_RECEIVER_WATCHDOG_COOLDOWN_MS", 600, 100, 10000);
}

RtcStreamSession::~RtcStreamSession() {
    Close();
}

void RtcStreamSession::SetFrameSink(FrameSink sink) {
    std::lock_guard<std::mutex> lk(mu_);
    frame_sink_ = std::move(sink);
}

void RtcStreamSession::SetStateSink(StateSink sink) {
    std::lock_guard<std::mutex> lk(mu_);
    state_sink_ = std::move(sink);
}

void RtcStreamSession::EmitState(rflow_stream_state_t state, rflow_err_t reason) {
    stream_state_.store(state, std::memory_order_release);
    StateSink s;
    {
        std::lock_guard<std::mutex> lk(mu_);
        s = state_sink_;
    }
    if (s) s(state, reason);
}

bool RtcStreamSession::Start() {
    if (closed_.load(std::memory_order_acquire)) return false;
    if (signaling_) return true;

    rflow::signal::SessionConfig config;
    config.server_addr = signaling_url_;
    config.registration.role = rflow::signal::PeerRole::kSubscriber;
    config.registration.device_id = device_id_;
    config.registration.stream_index = index_;

    signaling_ = std::make_unique<SignalingClient>(std::move(config));
    signaling_->SetDelegate(this);

    EmitState(RFLOW_STREAM_OPENING, RFLOW_OK);
    if (!signaling_->Start()) {
        RFLOW_LOGE("[pull idx=%d] signaling start failed", index_);
        signaling_.reset();
        EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_CONN_NETWORK);
        return false;
    }

    {
        const std::string dev =
            device_id_.empty() ? std::string(RFLOW_DEFAULT_DEVICE_ID) : device_id_;
        const std::string room = dev + ":" + std::to_string(index_);
        RFLOW_LOGI("[pull idx=%d] signaling room=%s (须与推流 stream_id 一致), waiting for offer...",
                   index_, room.c_str());
    }

    // 周期 stats 线程负责 watchdog + 1 Hz pipeline 日志：watchdog 默认开（保活），
    // pipeline 日志由 RFLOW_LOG_TIMING 控制；只要任一启用就拉起线程。
    if (watchdog_enabled_ || rflow::timing_log::IsEnabled()) {
        StartWatchdogThread();
    }
    return true;
}

void RtcStreamSession::Close() {
    if (closed_.exchange(true, std::memory_order_acq_rel)) return;

    StopWatchdogThread();

    {
        std::lock_guard<std::mutex> lk(mu_);
        pending_remote_ice_.clear();
        if (current_video_track_ && frame_adapter_) {
            current_video_track_->RemoveSink(frame_adapter_.get());
        }
        current_video_track_ = nullptr;
    }
    remote_description_applied_.store(false, std::memory_order_release);
    pending_set_remote_observer_ = nullptr;
    pending_create_answer_observer_ = nullptr;
    pending_set_local_observer_ = nullptr;

    if (signaling_) {
        signaling_->SetDelegate(nullptr);
        signaling_->Stop();
        signaling_.reset();
    }

    if (peer_connection_) {
        peer_connection_->Close();
        peer_connection_ = nullptr;
    }

    EmitState(RFLOW_STREAM_CLOSED, RFLOW_OK);
}

void RtcStreamSession::OnSignalMessage(const rflow::signal::Message& msg) {
    switch (msg.type) {
        case rflow::signal::MessageType::kOffer:
            HandleOffer(msg.sdp);
            return;
        case rflow::signal::MessageType::kIce:
            HandleRemoteIceCandidate(msg.mid, msg.mline_index, msg.candidate);
            return;
        default:
            return;
    }
}

void RtcStreamSession::OnSignalError(std::string_view error) {
    RFLOW_LOGW("[pull idx=%d] signaling error: %.*s",
               index_,
               static_cast<int>(error.size()),
               error.data());
    EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_CONN_NETWORK);
}

void RtcStreamSession::CreatePeerConnectionLocked() {
    if (!factory_) {
        RFLOW_LOGE("[pull idx=%d] factory null", index_);
        return;
    }

    if (peer_connection_) {
        peer_connection_->Close();
        peer_connection_ = nullptr;
    }

    webrtc::PeerConnectionInterface::RTCConfiguration config;
    config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    // 接收端为纯视频拉流；把 audio NetEQ 的最大缓冲包数压到 1，开 fast_accelerate，
    // 避免极小 audio 播放也有不必要的 jitter buffer 开销，与视频策略对齐。
    config.audio_jitter_buffer_max_packets        = 1;
    config.audio_jitter_buffer_min_delay_ms       = 0;
    config.audio_jitter_buffer_fast_accelerate    = true;

    webrtc::PeerConnectionInterface::IceServer ice_server;
    ice_server.urls.push_back("stun:stun.l.google.com:19302");
    config.servers.push_back(ice_server);

    webrtc::PeerConnectionDependencies deps(observer_.get());
    auto result = factory_->CreatePeerConnectionOrError(config, std::move(deps));
    if (!result.ok()) {
        RFLOW_LOGE("[pull idx=%d] CreatePeerConnection failed: %s",
                   index_, result.error().message());
        EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_FAIL);
        return;
    }

    peer_connection_ = result.MoveValue();

    webrtc::RtpTransceiverInit init;
    init.direction = webrtc::RtpTransceiverDirection::kRecvOnly;

    auto* signaling_thread = peer_connection_->signaling_thread();
    if (!signaling_thread) {
        RFLOW_LOGE("[pull idx=%d] peer connection signaling thread null", index_);
        peer_connection_->Close();
        peer_connection_ = nullptr;
        EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_FAIL);
        return;
    }

    auto add_video_transceiver = [this, init]() mutable {
        if (!peer_connection_) {
            return;
        }

        auto transceiver_or_error =
            peer_connection_->AddTransceiver(webrtc::MediaType::VIDEO, init);
        if (!transceiver_or_error.ok()) {
            RFLOW_LOGE("[pull idx=%d] AddTransceiver(video recvonly) failed: %s",
                       index_, transceiver_or_error.error().message());
            peer_connection_->Close();
            peer_connection_ = nullptr;
            EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_FAIL);
            return;
        }

        RFLOW_LOGI("[pull idx=%d] peer connection ready with recvonly video transceiver",
                   index_);
    };

    if (signaling_thread->IsCurrent()) {
        add_video_transceiver();
    } else {
        signaling_thread->BlockingCall(add_video_transceiver);
    }
}

bool RtcStreamSession::RunOnPeerConnectionSignalingThread(const std::function<void()>& task) {
    if (!task) {
        return false;
    }
    if (!peer_connection_) {
        RFLOW_LOGW("[pull idx=%d] peer connection null", index_);
        return false;
    }

    auto* signaling_thread = peer_connection_->signaling_thread();
    if (!signaling_thread) {
        RFLOW_LOGE("[pull idx=%d] peer connection signaling thread null", index_);
        return false;
    }

    if (signaling_thread->IsCurrent()) {
        task();
    } else {
        signaling_thread->BlockingCall([task] { task(); });
    }
    return true;
}

void RtcStreamSession::HandleOffer(const std::string& sdp) {
    if (closed_.load(std::memory_order_acquire)) return;
    RFLOW_LOGI("[pull idx=%d] recv offer, creating answer...", index_);

    {
        std::lock_guard<std::mutex> lk(mu_);
        pending_remote_ice_.clear();
    }
    remote_description_applied_.store(false, std::memory_order_release);
    pending_set_remote_observer_ = nullptr;
    pending_create_answer_observer_ = nullptr;
    pending_set_local_observer_ = nullptr;

    CreatePeerConnectionLocked();
    if (!peer_connection_) return;

    webrtc::SdpParseError err;
    auto remote = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, sdp, &err);
    if (!remote) {
        RFLOW_LOGE("[pull idx=%d] parse remote sdp failed: %s",
                   index_, err.description.c_str());
        EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_PARAM);
        return;
    }

    auto self = shared_from_this();
    pending_set_remote_observer_ = webrtc::make_ref_counted<SetRemoteDescObserver>(
        [self](webrtc::RTCError error) {
            self->pending_set_remote_observer_ = nullptr;
            if (!error.ok()) {
                RFLOW_LOGE("[pull idx=%d] SetRemoteDescription failed: %s",
                           self->index_, error.message());
                self->remote_description_applied_.store(false, std::memory_order_release);
                {
                    std::lock_guard<std::mutex> lk(self->mu_);
                    self->pending_remote_ice_.clear();
                }
                self->EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_FAIL);
                return;
            }

            self->remote_description_applied_.store(true, std::memory_order_release);
            self->FlushPendingRemoteIceCandidates();
            self->DoCreateAnswerAfterSetRemote();
        });

    auto* signaling_thread = peer_connection_->signaling_thread();
    if (!signaling_thread) {
        pending_set_remote_observer_ = nullptr;
        {
            std::lock_guard<std::mutex> lk(mu_);
            pending_remote_ice_.clear();
        }
        RFLOW_LOGE("[pull idx=%d] peer connection signaling thread null", index_);
        EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_FAIL);
        return;
    }

    auto apply_remote = [this, remote_desc = std::move(remote)]() mutable {
        if (!peer_connection_) {
            return;
        }
        peer_connection_->SetRemoteDescription(std::move(remote_desc),
                                               pending_set_remote_observer_);
    };

    if (signaling_thread->IsCurrent()) {
        apply_remote();
    } else {
        signaling_thread->BlockingCall(std::move(apply_remote));
    }
}

void RtcStreamSession::DoCreateAnswerAfterSetRemote() {
    if (!peer_connection_) return;

    // 把 H264 constrained-baseline 推到首选，便于 Android MediaCodec / RK MPP 等硬解走低延迟路径。
    if (factory_) {
        const webrtc::RtpCapabilities caps =
            factory_->GetRtpReceiverCapabilities(webrtc::MediaType::VIDEO);
        std::vector<webrtc::RtpCodecCapability> preferred =
            BuildLowLatencyVideoCodecPreferences(caps);
        if (!preferred.empty()) {
            for (const auto& transceiver : peer_connection_->GetTransceivers()) {
                if (!transceiver || transceiver->media_type() != webrtc::MediaType::VIDEO) {
                    continue;
                }
                const webrtc::RTCError err = transceiver->SetCodecPreferences(preferred);
                if (!err.ok()) {
                    RFLOW_LOGW("[pull idx=%d] SetCodecPreferences failed: %s",
                               index_, err.message());
                } else {
                    RFLOW_LOGI("[pull idx=%d] SetCodecPreferences ok, codecs=%zu",
                               index_, preferred.size());
                }
            }
        } else {
            RFLOW_LOGD("[pull idx=%d] no preferred low-latency video codec list, skip "
                       "SetCodecPreferences", index_);
        }
    }

    auto self = shared_from_this();
    pending_create_answer_observer_ = webrtc::make_ref_counted<CreateAnswerObserver>(
        [self](webrtc::RTCError e,
               std::unique_ptr<webrtc::SessionDescriptionInterface> desc) {
            self->pending_create_answer_observer_ = nullptr;
            if (!e.ok() || !desc) {
                RFLOW_LOGE("[pull idx=%d] CreateAnswer failed: %s",
                           self->index_, e.ok() ? "desc null" : e.message());
                self->EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_FAIL);
                return;
            }

            std::string answer_sdp;
            if (!desc->ToString(&answer_sdp) || answer_sdp.empty()) {
                RFLOW_LOGE("[pull idx=%d] answer sdp ToString failed", self->index_);
                self->EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_FAIL);
                return;
            }
            RFLOW_LOGI("[pull idx=%d] CreateAnswer ok, sdp_bytes=%zu",
                       self->index_, answer_sdp.size());

            self->pending_set_local_observer_ = webrtc::make_ref_counted<SetLocalDescObserver>(
                [self, answer_sdp]() {
                    self->pending_set_local_observer_ = nullptr;
                    if (!self->signaling_) {
                        RFLOW_LOGW("[pull idx=%d] SetLocal ok but signaling gone", self->index_);
                        return;
                    }

                    rflow::signal::Message msg;
                    msg.type = rflow::signal::MessageType::kAnswer;
                    msg.sdp = answer_sdp;
                    if (!self->signaling_->Send(msg)) {
                        RFLOW_LOGE("[pull idx=%d] send answer failed", self->index_);
                        self->EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_CONN_NETWORK);
                        return;
                    }
                    RFLOW_LOGI("[pull idx=%d] answer sent", self->index_);
                },
                [self](webrtc::RTCError er) {
                    self->pending_set_local_observer_ = nullptr;
                    RFLOW_LOGE("[pull idx=%d] SetLocalDescription failed: %s",
                               self->index_, er.message());
                    self->EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_FAIL);
                });
            self->peer_connection_->SetLocalDescription(
                self->pending_set_local_observer_.get(), desc.release());
        },
        [self](webrtc::RTCError e) {
            self->pending_create_answer_observer_ = nullptr;
            RFLOW_LOGE("[pull idx=%d] CreateAnswer OnFailure: %s", self->index_, e.message());
            self->EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_FAIL);
        });

    if (!RunOnPeerConnectionSignalingThread([this]() {
            if (!peer_connection_) {
                return;
            }
            webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
            options.offer_to_receive_audio = 0;
            options.num_simulcast_layers = 1;
            peer_connection_->CreateAnswer(pending_create_answer_observer_.get(), options);
        })) {
        pending_create_answer_observer_ = nullptr;
        EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_FAIL);
    }
}

void RtcStreamSession::AddRemoteIceCandidateNow(const std::string& mid,
                                                int                mline_index,
                                                const std::string& candidate) {
    if (!peer_connection_) return;

    RunOnPeerConnectionSignalingThread([this, mid, mline_index, candidate]() {
        if (!peer_connection_) {
            return;
        }

        webrtc::SdpParseError err;
        webrtc::IceCandidateInterface* cand =
            webrtc::CreateIceCandidate(mid, mline_index, candidate, &err);
        if (!cand) {
            RFLOW_LOGW("[pull idx=%d] CreateIceCandidate failed: %s mid=%s mline=%d",
                       index_, err.description.c_str(), mid.c_str(), mline_index);
            return;
        }

        const bool ok = peer_connection_->AddIceCandidate(cand);
        delete cand;
        if (!ok) {
            RFLOW_LOGW("[pull idx=%d] AddIceCandidate returned false mid=%s mline=%d",
                       index_, mid.c_str(), mline_index);
        }
    });
}

void RtcStreamSession::FlushPendingRemoteIceCandidates() {
    std::vector<PendingRemoteIce> pending;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (pending_remote_ice_.empty()) return;
        pending.swap(pending_remote_ice_);
    }

    RFLOW_LOGD("[pull idx=%d] flush pending ice, n=%zu",
               index_, pending.size());
    for (const auto& p : pending) {
        AddRemoteIceCandidateNow(p.mid, p.mline_index, p.candidate);
    }
}

void RtcStreamSession::HandleRemoteIceCandidate(const std::string& mid,
                                                int                mline_index,
                                                const std::string& candidate) {
    if (!peer_connection_ || !remote_description_applied_.load(std::memory_order_acquire)) {
        size_t pending_count = 0;
        {
            std::lock_guard<std::mutex> lk(mu_);
            pending_remote_ice_.push_back(PendingRemoteIce{mid, mline_index, candidate});
            pending_count = pending_remote_ice_.size();
        }
        RFLOW_LOGD("[pull idx=%d] enqueue remote ice, size=%zu",
                   index_, pending_count);
        return;
    }

    AddRemoteIceCandidateNow(mid, mline_index, candidate);
}

bool RtcStreamSession::CollectStats(librflow_stream_stats_s* out_stats) {
    if (!out_stats || !peer_connection_) {
        return false;
    }

    struct Snapshot {
        bool done = false;
        uint64_t in_bytes = 0;
        uint64_t in_pkts = 0;
        uint32_t lost_pkts = 0;
        uint32_t fps = 0;
        uint32_t jitter_ms = 0;
        uint32_t freeze_count = 0;
        uint32_t decode_fail_count = 0;
        uint32_t rtt_ms = 0;
        uint32_t bitrate_kbps = 0;
    };

    std::mutex mu;
    std::condition_variable cv;
    Snapshot snapshot;

    auto callback = webrtc::make_ref_counted<StatsCollectorCallback>(
        [&mu, &cv, &snapshot](const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
            Snapshot local;
            if (report) {
                for (const auto* inbound : report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>()) {
                    if (!inbound || !inbound->kind || *inbound->kind != "video") continue;

                    if (inbound->bytes_received) local.in_bytes += *inbound->bytes_received;
                    if (inbound->packets_received) local.in_pkts += *inbound->packets_received;
                    if (inbound->packets_lost && *inbound->packets_lost > 0) {
                        local.lost_pkts += static_cast<uint32_t>(*inbound->packets_lost);
                    }
                    if (inbound->frames_per_second) {
                        local.fps = std::max(local.fps,
                                             static_cast<uint32_t>(*inbound->frames_per_second + 0.5));
                    }
                    if (inbound->jitter) {
                        local.jitter_ms = std::max(
                            local.jitter_ms, static_cast<uint32_t>(*inbound->jitter * 1000.0 + 0.5));
                    }
                    if (inbound->freeze_count) {
                        local.freeze_count = std::max(local.freeze_count, *inbound->freeze_count);
                    }
                    if (inbound->frames_dropped) {
                        local.decode_fail_count =
                            std::max(local.decode_fail_count, *inbound->frames_dropped);
                    }
                }

                for (const auto* pair : report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>()) {
                    if (!pair) continue;
                    if (pair->current_round_trip_time) {
                        local.rtt_ms = std::max(
                            local.rtt_ms,
                            static_cast<uint32_t>(*pair->current_round_trip_time * 1000.0 + 0.5));
                    }
                    if (pair->available_incoming_bitrate) {
                        local.bitrate_kbps = std::max(
                            local.bitrate_kbps,
                            static_cast<uint32_t>(*pair->available_incoming_bitrate / 1000.0 + 0.5));
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lk(mu);
                snapshot = local;
                snapshot.done = true;
            }
            cv.notify_one();
        });

    peer_connection_->GetStats(callback.get());

    std::unique_lock<std::mutex> lk(mu);
    if (!cv.wait_for(lk, std::chrono::milliseconds(1500), [&snapshot] { return snapshot.done; })) {
        return false;
    }

    out_stats->in_bound_bytes = snapshot.in_bytes;
    out_stats->in_bound_pkts = snapshot.in_pkts;
    out_stats->lost_pkts = snapshot.lost_pkts;
    out_stats->fps = snapshot.fps;
    out_stats->jitter_ms = snapshot.jitter_ms;
    out_stats->freeze_count = snapshot.freeze_count;
    out_stats->decode_fail_count = snapshot.decode_fail_count;
    out_stats->rtt_ms = snapshot.rtt_ms;
    out_stats->bitrate_kbps = snapshot.bitrate_kbps;
    return true;
}

// =====================================================================
// Keyframe-recovery watchdog
// =====================================================================
void RtcStreamSession::StartWatchdogThread() {
    bool expected = false;
    if (!stats_running_.compare_exchange_strong(expected, true,
                                                 std::memory_order_acq_rel)) {
        return;
    }
    prev_frames_decoded_           = 0;
    prev_packets_received_         = 0;
    last_decode_progress_mono_ms_  = MonoTimeMs();
    last_keyframe_kick_mono_ms_    = 0;
    last_keyframe_kick_packets_    = 0;
    stats_thread_ = std::thread([this] { RunWatchdogLoop(); });
}

void RtcStreamSession::StopWatchdogThread() {
    if (!stats_running_.exchange(false, std::memory_order_acq_rel)) return;
    stats_cv_.notify_all();
    if (stats_thread_.joinable() &&
        stats_thread_.get_id() != std::this_thread::get_id()) {
        stats_thread_.join();
    }
}

void RtcStreamSession::RunWatchdogLoop() {
    while (stats_running_.load(std::memory_order_acquire)) {
        TickWatchdog();
        std::unique_lock<std::mutex> lk(stats_cv_mu_);
        stats_cv_.wait_for(lk, std::chrono::seconds(1), [this] {
            return !stats_running_.load(std::memory_order_acquire);
        });
    }
}

void RtcStreamSession::TickWatchdog() {
    if (!peer_connection_) return;
    auto self = shared_from_this();
    auto cb = webrtc::make_ref_counted<StatsCollectorCallback>(
        [self](const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
            self->OnWatchdogStats(report);
        });
    peer_connection_->GetStats(cb.get());
}

void RtcStreamSession::OnWatchdogStats(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
    if (!report || !peer_connection_ || closed_.load(std::memory_order_acquire)) return;

    // ---- 单次扫描：watchdog 与周期日志共用同一组 inbound video 累加 ----
    uint64_t frames_decoded     = 0;
    uint64_t packets_received   = 0;
    int64_t  packets_lost_signed = 0;  // RFC 3550 signed
    uint64_t frames_dropped     = 0;
    uint64_t bytes_received     = 0;
    uint64_t fec_packets        = 0;
    uint32_t nack_count         = 0;
    uint32_t pli_count          = 0;
    uint32_t fir_count          = 0;
    uint32_t frame_w            = 0;
    uint32_t frame_h            = 0;
    double   fps                = 0.0;
    double   total_decode_time_s = 0.0;
    double   total_processing_delay_s = 0.0;
    double   total_assembly_time_s    = 0.0;
    double   jitter_s           = 0.0;
    double   jb_delay_s         = 0.0;
    uint64_t jb_emitted         = 0;
    std::string decoder_impl;
    std::string codec_id;
    bool     have_video         = false;

    for (const auto* inb : report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>()) {
        if (!inb || !inb->kind || *inb->kind != "video") continue;
        if (inb->frames_decoded)        frames_decoded     += static_cast<uint64_t>(*inb->frames_decoded);
        if (inb->packets_received)      packets_received   += static_cast<uint64_t>(*inb->packets_received);
        if (inb->packets_lost)          packets_lost_signed += static_cast<int64_t>(*inb->packets_lost);
        if (inb->frames_dropped)        frames_dropped     += static_cast<uint64_t>(*inb->frames_dropped);
        if (inb->bytes_received)        bytes_received     += *inb->bytes_received;
        if (inb->fec_packets_received)  fec_packets        += *inb->fec_packets_received;
        if (inb->nack_count)            nack_count          = std::max(nack_count, *inb->nack_count);
        if (inb->pli_count)             pli_count           = std::max(pli_count,  *inb->pli_count);
        if (inb->fir_count)             fir_count           = std::max(fir_count,  *inb->fir_count);
        if (inb->frame_width)           frame_w             = std::max(frame_w,    *inb->frame_width);
        if (inb->frame_height)          frame_h             = std::max(frame_h,    *inb->frame_height);
        if (inb->frames_per_second)     fps                 = std::max(fps,        *inb->frames_per_second);
        if (inb->total_decode_time)     total_decode_time_s += *inb->total_decode_time;
        if (inb->total_processing_delay) total_processing_delay_s += *inb->total_processing_delay;
        if (inb->total_assembly_time)    total_assembly_time_s    += *inb->total_assembly_time;
        if (inb->jitter)                jitter_s            = std::max(jitter_s,   *inb->jitter);
        if (inb->jitter_buffer_delay)         jb_delay_s   += *inb->jitter_buffer_delay;
        if (inb->jitter_buffer_emitted_count) jb_emitted   += *inb->jitter_buffer_emitted_count;
        if (inb->decoder_implementation && decoder_impl.empty()) {
            decoder_impl = *inb->decoder_implementation;
        }
        if (inb->codec_id && codec_id.empty()) {
            codec_id = *inb->codec_id;
        }
        have_video = true;
    }
    if (!have_video) return;

    // ---- watchdog ----
    const int64_t now_ms = MonoTimeMs();
    const uint64_t delta_frames = (frames_decoded >= prev_frames_decoded_)
                                       ? (frames_decoded - prev_frames_decoded_)
                                       : 0;

    if (watchdog_enabled_) {
        if (delta_frames > 0) {
            last_decode_progress_mono_ms_ = now_ms;
            last_keyframe_kick_packets_   = packets_received;
        } else if (last_decode_progress_mono_ms_ == 0) {
            last_decode_progress_mono_ms_ = now_ms;
        }

        const int64_t stuck_ms          = now_ms - last_decode_progress_mono_ms_;
        const bool packets_still_flowing = packets_received > last_keyframe_kick_packets_;
        const bool cooldown_elapsed = last_keyframe_kick_mono_ms_ == 0 ||
                                       (now_ms - last_keyframe_kick_mono_ms_) >= watchdog_cooldown_ms_;

        if (delta_frames == 0 && packets_still_flowing &&
            stuck_ms >= watchdog_stuck_threshold_ms_ && cooldown_elapsed) {
            RFLOW_LOGW("[pull idx=%d] keyframe watchdog: decoder stalled %lld ms, packets=%llu; "
                       "kicking jitter buffer to force PLI",
                       index_, static_cast<long long>(stuck_ms),
                       static_cast<unsigned long long>(packets_received));
            KickJitterBufferForKeyframe();
            last_keyframe_kick_mono_ms_   = now_ms;
            last_keyframe_kick_packets_   = packets_received;
        }
    }

    // ---- 1 Hz 流水线日志 ----
    // 受 RFLOW_LOG_TIMING 总开关控制（默认关），与 [Timing/Decode] 共用一个 env。
    if (rflow::timing_log::IsEnabled()) {
        // RTT（取活跃的 candidate pair；若多对则取 nominated 优先，其它取最大）
        double cur_rtt_s   = 0.0;
        double total_rtt_s = 0.0;
        uint64_t rtt_resp  = 0;
        bool     have_pair = false;
        for (const auto* pair : report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>()) {
            if (!pair) continue;
            const bool nominated = pair->nominated && *pair->nominated;
            if (have_pair && !nominated) continue;
            if (pair->current_round_trip_time) cur_rtt_s   = *pair->current_round_trip_time;
            if (pair->total_round_trip_time)   total_rtt_s = *pair->total_round_trip_time;
            if (pair->responses_received)      rtt_resp    = *pair->responses_received;
            if (nominated) { have_pair = true; }
            else if (!have_pair) { have_pair = true; }
        }
        const double cur_rtt_ms = cur_rtt_s * 1000.0;
        const double avg_rtt_ms = (rtt_resp > 0) ? (total_rtt_s / static_cast<double>(rtt_resp)) * 1000.0
                                                  : cur_rtt_ms;

        // 解码与丢帧的增量；首次 tick 仅记录基线，避免“+全部累计”这种误导值。
        const uint64_t d_decoded =
            stats_log_baseline_done_ && frames_decoded >= prev_frames_decoded_
                ? frames_decoded - prev_frames_decoded_ : 0;
        const uint64_t d_dropped =
            stats_log_baseline_done_ && frames_dropped >= prev_frames_dropped_
                ? frames_dropped - prev_frames_dropped_ : 0;
        const double d_decode_time_s =
            stats_log_baseline_done_ && total_decode_time_s >= prev_total_decode_time_s_
                ? total_decode_time_s - prev_total_decode_time_s_ : 0.0;
        const double d_processing_s =
            stats_log_baseline_done_ && total_processing_delay_s >= prev_total_processing_delay_s_
                ? total_processing_delay_s - prev_total_processing_delay_s_ : 0.0;
        const double d_assembly_s =
            stats_log_baseline_done_ && total_assembly_time_s >= prev_total_assembly_time_s_
                ? total_assembly_time_s - prev_total_assembly_time_s_ : 0.0;
        const double avg_decode_ms =
            (d_decoded > 0) ? (d_decode_time_s * 1000.0 / static_cast<double>(d_decoded)) : 0.0;
        const double avg_processing_ms =
            (d_decoded > 0) ? (d_processing_s * 1000.0 / static_cast<double>(d_decoded)) : 0.0;
        const double avg_assembly_ms =
            (d_decoded > 0) ? (d_assembly_s * 1000.0 / static_cast<double>(d_decoded)) : 0.0;

        const double jb_delay_ms =
            (jb_emitted > 0) ? (jb_delay_s * 1000.0 / static_cast<double>(jb_emitted)) : 0.0;
        const uint64_t bytes_kb = bytes_received / 1024ULL;

        const int64_t lost = packets_lost_signed;
        const uint64_t pos_lost = (lost > 0) ? static_cast<uint64_t>(lost) : 0;
        const uint64_t total_for_loss = packets_received + pos_lost;
        const double loss_pct = (total_for_loss > 0)
            ? (static_cast<double>(pos_lost) * 100.0 / static_cast<double>(total_for_loss))
            : 0.0;
        const double jitter_ms = jitter_s * 1000.0;

        const char* decoder_cstr = decoder_impl.empty() ? "unknown" : decoder_impl.c_str();

        // 解析 codec 引用（从 inbound stat 拿到 codec_id 后查 RTCCodecStats）
        std::string codec_mime;
        std::string codec_fmtp;
        uint32_t    codec_pt = 0;
        bool        has_codec_pt = false;
        if (!codec_id.empty()) {
            if (const auto* codec = report->GetAs<webrtc::RTCCodecStats>(codec_id)) {
                if (codec->mime_type)     codec_mime = *codec->mime_type;
                if (codec->sdp_fmtp_line) codec_fmtp = *codec->sdp_fmtp_line;
                if (codec->payload_type) {
                    codec_pt     = *codec->payload_type;
                    has_codec_pt = true;
                }
            }
        }

        const std::string codec_label =
            codec_mime.empty()
                ? std::string("unknown")
                : (has_codec_pt ? codec_mime + " pt=" + std::to_string(codec_pt) : codec_mime);

        // [Pipeline/Video]：每秒视频/解码总账（合并旧 [DecodeStats] 中文重复的字段）。
        RFLOW_LOGD(
            "[Pipeline/Video] %ux%u | decoder=%s | codec=%s | fps=%.1f | "
            "decoded=+%llu total=%llu | dropped=+%llu total=%llu | recv=%llu KB",
            frame_w, frame_h, decoder_cstr, codec_label.c_str(), fps,
            static_cast<unsigned long long>(d_decoded),
            static_cast<unsigned long long>(frames_decoded),
            static_cast<unsigned long long>(d_dropped),
            static_cast<unsigned long long>(frames_dropped),
            static_cast<unsigned long long>(bytes_kb));

        // [Pipeline/Latency]：每秒延迟分项（合并旧 jb / decode_avg；新增 processing/assembly）。
        const double jitter_min_ms =
            jitter_min_delay_seconds_.load(std::memory_order_relaxed) * 1000.0;
        RFLOW_LOGD(
            "[Pipeline/Latency] jb_avg=%.1f ms | decode_avg=%.2f ms | "
            "processing_avg=%.2f ms | assembly_avg=%.2f ms | jitter_min=%.1f ms",
            jb_delay_ms, avg_decode_ms, avg_processing_ms, avg_assembly_ms, jitter_min_ms);

        // [Pipeline/Codec]：仅在 fmtp/mime/pt 首次出现或变化时打印一次（不再每秒重复）。
        if (codec_mime != last_codec_mime_ || codec_fmtp != last_codec_fmtp_ ||
            codec_pt != last_codec_payload_type_) {
            if (!codec_mime.empty() || !codec_fmtp.empty()) {
                RFLOW_LOGD("[Pipeline/Codec] mime=%s pt=%u fmtp=%s",
                           codec_mime.empty() ? "unknown" : codec_mime.c_str(),
                           codec_pt,
                           codec_fmtp.empty() ? "" : codec_fmtp.c_str());
            }
            last_codec_mime_         = codec_mime;
            last_codec_fmtp_         = codec_fmtp;
            last_codec_payload_type_ = codec_pt;
        }

        // [Pipeline/Net]：每秒网络分项（与之前一致；删除中文重复的 [NetStats]）。
        RFLOW_LOGD(
            "[Pipeline/Net] rtt=%.1f ms | rtt_avg=%.1f ms | jitter=%.1f ms | "
            "loss=%.2f%% (%lld/%llu) | nack=%u | pli=%u | fir=%u | fec=%llu",
            cur_rtt_ms, avg_rtt_ms, jitter_ms, loss_pct,
            static_cast<long long>(lost),
            static_cast<unsigned long long>(total_for_loss),
            nack_count, pli_count, fir_count,
            static_cast<unsigned long long>(fec_packets));

        prev_frames_dropped_           = frames_dropped;
        prev_total_decode_time_s_      = total_decode_time_s;
        prev_total_processing_delay_s_ = total_processing_delay_s;
        prev_total_assembly_time_s_    = total_assembly_time_s;
        stats_log_baseline_done_       = true;
    }

    prev_frames_decoded_   = frames_decoded;
    prev_packets_received_ = packets_received;
}

void RtcStreamSession::KickJitterBufferForKeyframe() {
    if (!peer_connection_) return;
    const double floor_s = jitter_min_delay_seconds_.load(std::memory_order_acquire);
    // 临时 bump 到 300ms 再回落到 floor，触发 RtpVideoStreamReceiver 重新评估帧状态、
    // 必要时直接发 PLI/FIR；这一抖动对感知延迟的扰动 < 1 个 stats 间隔。
    auto kick = [this, floor_s]() {
        if (!peer_connection_) return;
        for (const auto& transceiver : peer_connection_->GetTransceivers()) {
            if (!transceiver || transceiver->media_type() != webrtc::MediaType::VIDEO) {
                continue;
            }
            auto receiver = transceiver->receiver();
            if (!receiver) continue;
            receiver->SetJitterBufferMinimumDelay(std::optional<double>(0.3));
            receiver->SetJitterBufferMinimumDelay(std::optional<double>(floor_s));
        }
    };
    RunOnPeerConnectionSignalingThread(kick);
}

}  // namespace rflow::client::impl

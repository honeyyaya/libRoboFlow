#include "webrtc_pull_stream.h"

#include "signaling_client.h"
#include "webrtc_factory.h"

#include "common/internal/logger.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/set_remote_description_observer_interface.h"
#include "rtc_base/thread.h"
#include "system_wrappers/include/field_trial.h"

namespace robrt::client::impl {

namespace {

webrtc::SdpType SdpTypeFromOfferAnswerString(const std::string& t) {
    auto opt = webrtc::SdpTypeFromString(t);
    if (opt) return *opt;
    if (t == "offer")    return webrtc::SdpType::kOffer;
    if (t == "answer")   return webrtc::SdpType::kAnswer;
    if (t == "pranswer") return webrtc::SdpType::kPrAnswer;
    return webrtc::SdpType::kOffer;
}

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
        if (cb_) cb_(webrtc::RTCError::OK(),
                     std::unique_ptr<webrtc::SessionDescriptionInterface>(desc));
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

    void OnSuccess() override { if (ok_) ok_(); }
    void OnFailure(webrtc::RTCError error) override {
        if (fail_) fail_(std::move(error));
    }

 private:
    std::function<void()>                 ok_;
    std::function<void(webrtc::RTCError)> fail_;
};

}  // namespace

// ---------------------------------------------------------------- FrameAdapter
class WebRtcPullStream::FrameAdapter final
    : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
    explicit FrameAdapter(WebRtcPullStream* owner) : owner_(owner) {}

    void OnFrame(const webrtc::VideoFrame& frame) override {
        FrameSink sink;
        {
            std::lock_guard<std::mutex> lk(owner_->mu_);
            sink = owner_->frame_sink_;
        }
        if (sink) sink(frame);
        // TODO: 在这里对接 librobrt_stream_cb::on_video，走 SDK 统一分发线程。
    }

 private:
    WebRtcPullStream* owner_;
};

// ------------------------------------------------- PeerConnectionObserverImpl
class WebRtcPullStream::PeerConnectionObserverImpl : public webrtc::PeerConnectionObserver {
 public:
    explicit PeerConnectionObserverImpl(WebRtcPullStream* s) : stream_(s) {}

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}

    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState s) override {
        ROBRT_LOGD("[pull idx=%d] IceGatheringState=%d", stream_->index_, static_cast<int>(s));
    }

    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override {
        if (!candidate || !stream_->signaling_) return;
        std::string sdp;
        if (!candidate->ToString(&sdp)) return;
        stream_->signaling_->SendIceCandidate(candidate->sdp_mid(),
                                              candidate->sdp_mline_index(), sdp);
    }

    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState s) override {
        using PCS = webrtc::PeerConnectionInterface::PeerConnectionState;
        if (s == PCS::kConnected) {
            stream_->EmitState(ROBRT_STREAM_OPENED, ROBRT_OK);
            // TODO: 启动 stats 采集（robrt_stream_stats_t 周期派发）。
        } else if (s == PCS::kFailed) {
            stream_->EmitState(ROBRT_STREAM_FAILED, ROBRT_ERR_CONN_FAIL);
        } else if (s == PCS::kDisconnected || s == PCS::kClosed) {
            stream_->EmitState(ROBRT_STREAM_CLOSED, ROBRT_OK);
        }
    }

    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState s) override {
        if (s == webrtc::PeerConnectionInterface::kIceConnectionFailed) {
            ROBRT_LOGW("[pull idx=%d] ICE connection failed", stream_->index_);
        }
    }

    void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override {
        if (!transceiver || transceiver->media_type() != webrtc::MediaType::VIDEO) return;
        auto receiver = transceiver->receiver();
        if (!receiver) return;
        receiver->SetJitterBufferMinimumDelay(std::optional<double>(0.0));

        auto track = receiver->track();
        if (!track ||
            track->kind() != std::string(webrtc::MediaStreamTrackInterface::kVideoKind)) {
            return;
        }
        auto* vptr = static_cast<webrtc::VideoTrackInterface*>(track.get());
        webrtc::scoped_refptr<webrtc::VideoTrackInterface> v(vptr);

        stream_->current_video_track_ = v;
        if (stream_->frame_adapter_) {
            webrtc::VideoSinkWants wants;
            v->AddOrUpdateSink(stream_->frame_adapter_.get(), wants);
        }
    }

 private:
    WebRtcPullStream* stream_;
};

// ---------------------------------------------------------------- WebRtcPullStream
WebRtcPullStream::WebRtcPullStream(
    int32_t index,
    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory,
    std::string signaling_url,
    std::string role)
    : index_(index),
      signaling_url_(std::move(signaling_url)),
      role_(std::move(role)),
      factory_(std::move(factory)) {
    observer_      = std::make_unique<PeerConnectionObserverImpl>(this);
    frame_adapter_ = std::make_unique<FrameAdapter>(this);
}

WebRtcPullStream::~WebRtcPullStream() {
    Close();
}

void WebRtcPullStream::SetFrameSink(FrameSink sink) {
    std::lock_guard<std::mutex> lk(mu_);
    frame_sink_ = std::move(sink);
}

void WebRtcPullStream::SetStateSink(StateSink sink) {
    std::lock_guard<std::mutex> lk(mu_);
    state_sink_ = std::move(sink);
}

void WebRtcPullStream::EmitState(robrt_stream_state_t state, robrt_err_t reason) {
    stream_state_.store(state, std::memory_order_release);
    StateSink s;
    {
        std::lock_guard<std::mutex> lk(mu_);
        s = state_sink_;
    }
    if (s) s(state, reason);
}

bool WebRtcPullStream::Start() {
    if (closed_.load(std::memory_order_acquire)) return false;
    if (signaling_) return true;

    signaling_ = std::make_unique<SignalingClient>(signaling_url_, role_);

    // TODO: post 到 SDK 统一的回调线程；当前回调直接在 SignalingClient 读线程执行。
    signaling_->SetOnOffer([this](const std::string& type, const std::string& sdp) {
        HandleOffer(type, sdp);
    });
    signaling_->SetOnIce([this](const std::string& mid, int mline, const std::string& cand) {
        HandleRemoteIceCandidate(mid, mline, cand);
    });
    signaling_->SetOnError([this](const std::string& err) {
        ROBRT_LOGW("[pull idx=%d] signaling error: %s", index_, err.c_str());
        EmitState(ROBRT_STREAM_FAILED, ROBRT_ERR_CONN_NETWORK);
    });

    EmitState(ROBRT_STREAM_OPENING, ROBRT_OK);
    if (!signaling_->Start()) {
        ROBRT_LOGE("[pull idx=%d] signaling start failed", index_);
        signaling_.reset();
        EmitState(ROBRT_STREAM_FAILED, ROBRT_ERR_CONN_NETWORK);
        return false;
    }
    ROBRT_LOGI("[pull idx=%d] signaling started, waiting for offer...", index_);
    return true;
}

void WebRtcPullStream::Close() {
    if (closed_.exchange(true, std::memory_order_acq_rel)) return;

    pending_remote_ice_.clear();
    remote_description_applied_       = false;
    pending_set_remote_observer_      = nullptr;
    pending_create_answer_observer_   = nullptr;
    pending_set_local_observer_       = nullptr;

    if (current_video_track_ && frame_adapter_) {
        current_video_track_->RemoveSink(frame_adapter_.get());
    }
    current_video_track_ = nullptr;

    if (signaling_) {
        signaling_->Stop();
        signaling_.reset();
    }
    if (peer_connection_) {
        peer_connection_->Close();
        peer_connection_ = nullptr;
    }
    EmitState(ROBRT_STREAM_CLOSED, ROBRT_OK);
}

void WebRtcPullStream::EnsureFactoryFieldTrials() {
    // FieldTrials 必须在 CreatePeerConnectionFactory 前注册；全局至多一次。
    static bool inited = false;
    if (inited) return;
    static const std::string trials =
        "WebRTC-VideoFrameTrackingIdAdvertised/Enabled/";
    webrtc::field_trial::InitFieldTrialsFromString(trials.c_str());
    inited = true;
}

void WebRtcPullStream::CreatePeerConnectionLocked() {
    EnsureFactoryFieldTrials();
    if (!factory_) {
        ROBRT_LOGE("[pull idx=%d] factory null", index_);
        return;
    }

    if (peer_connection_) {
        peer_connection_->Close();
        peer_connection_ = nullptr;
    }

    webrtc::PeerConnectionInterface::RTCConfiguration config;
    config.sdp_semantics                    = webrtc::SdpSemantics::kUnifiedPlan;
    config.audio_jitter_buffer_min_delay_ms = 0;

    webrtc::PeerConnectionInterface::IceServer ice_server;
    // TODO: 从 global_config / signal_config 取 STUN/TURN，当前沿用旧 demo 默认。
    ice_server.urls.push_back("stun:stun.l.google.com:19302");
    config.servers.push_back(ice_server);

    webrtc::PeerConnectionDependencies deps(observer_.get());
    auto result = factory_->CreatePeerConnectionOrError(config, std::move(deps));
    if (!result.ok()) {
        ROBRT_LOGE("[pull idx=%d] CreatePeerConnection failed: %s",
                   index_, result.error().message());
        EmitState(ROBRT_STREAM_FAILED, ROBRT_ERR_FAIL);
        return;
    }
    peer_connection_ = result.MoveValue();
}

void WebRtcPullStream::HandleOffer(const std::string& type, const std::string& sdp) {
    if (closed_.load(std::memory_order_acquire)) return;
    ROBRT_LOGI("[pull idx=%d] recv offer, creating answer...", index_);

    pending_remote_ice_.clear();
    remote_description_applied_     = false;
    pending_set_remote_observer_    = nullptr;
    pending_create_answer_observer_ = nullptr;
    pending_set_local_observer_     = nullptr;

    CreatePeerConnectionLocked();
    if (!peer_connection_) return;

    webrtc::SdpParseError err;
    webrtc::SdpType sdp_type = SdpTypeFromOfferAnswerString(type);
    auto remote = webrtc::CreateSessionDescription(sdp_type, sdp, &err);
    if (!remote) {
        ROBRT_LOGE("[pull idx=%d] parse remote sdp failed: %s",
                   index_, err.description.c_str());
        EmitState(ROBRT_STREAM_FAILED, ROBRT_ERR_PARAM);
        return;
    }

    pending_set_remote_observer_ = webrtc::make_ref_counted<SetRemoteDescObserver>(
        [this](webrtc::RTCError error) {
            pending_set_remote_observer_ = nullptr;
            if (!error.ok()) {
                ROBRT_LOGE("[pull idx=%d] SetRemoteDescription failed: %s",
                           index_, error.message());
                remote_description_applied_ = false;
                pending_remote_ice_.clear();
                EmitState(ROBRT_STREAM_FAILED, ROBRT_ERR_FAIL);
                return;
            }
            remote_description_applied_ = true;
            FlushPendingRemoteIceCandidates();
            // 与旧实现一致：避免在 WebRTC 回调线程内直接 CreateAnswer 卡死，
            // 这里改为投递到 signaling 线程执行。
            // TODO: 统一到 SDK 的调度线程池。
            auto* th = PeerConnectionFactorySignalingThread();
            if (th) {
                th->PostTask([self = shared_from_this()] {
                    self->DoCreateAnswerAfterSetRemote();
                });
            } else {
                DoCreateAnswerAfterSetRemote();
            }
        });
    peer_connection_->SetRemoteDescription(std::move(remote), pending_set_remote_observer_);
}

void WebRtcPullStream::DoCreateAnswerAfterSetRemote() {
    if (!peer_connection_) return;

    pending_create_answer_observer_ = webrtc::make_ref_counted<CreateAnswerObserver>(
        [this](webrtc::RTCError e,
               std::unique_ptr<webrtc::SessionDescriptionInterface> desc) {
            pending_create_answer_observer_ = nullptr;
            if (!e.ok() || !desc) {
                ROBRT_LOGE("[pull idx=%d] CreateAnswer failed: %s",
                           index_, e.ok() ? "desc null" : e.message());
                EmitState(ROBRT_STREAM_FAILED, ROBRT_ERR_FAIL);
                return;
            }
            std::string answer_sdp;
            if (!desc->ToString(&answer_sdp) || answer_sdp.empty()) {
                ROBRT_LOGE("[pull idx=%d] answer sdp ToString failed", index_);
                EmitState(ROBRT_STREAM_FAILED, ROBRT_ERR_FAIL);
                return;
            }
            ROBRT_LOGI("[pull idx=%d] CreateAnswer ok, sdp_bytes=%zu",
                       index_, answer_sdp.size());

            pending_set_local_observer_ = webrtc::make_ref_counted<SetLocalDescObserver>(
                [this, answer_sdp]() {
                    pending_set_local_observer_ = nullptr;
                    if (!signaling_) {
                        ROBRT_LOGW("[pull idx=%d] SetLocal ok but signaling gone", index_);
                        return;
                    }
                    signaling_->SendAnswer(answer_sdp);
                    ROBRT_LOGI("[pull idx=%d] answer sent", index_);
                },
                [this](webrtc::RTCError er) {
                    pending_set_local_observer_ = nullptr;
                    ROBRT_LOGE("[pull idx=%d] SetLocalDescription failed: %s",
                               index_, er.message());
                    EmitState(ROBRT_STREAM_FAILED, ROBRT_ERR_FAIL);
                });
            peer_connection_->SetLocalDescription(pending_set_local_observer_.get(),
                                                   desc.release());
        },
        [this](webrtc::RTCError e) {
            pending_create_answer_observer_ = nullptr;
            ROBRT_LOGE("[pull idx=%d] CreateAnswer OnFailure: %s", index_, e.message());
            EmitState(ROBRT_STREAM_FAILED, ROBRT_ERR_FAIL);
        });
    peer_connection_->CreateAnswer(pending_create_answer_observer_.get(),
                                   webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
}

void WebRtcPullStream::AddRemoteIceCandidateNow(const std::string& mid, int mline_index,
                                                 const std::string& candidate) {
    if (!peer_connection_) return;
    webrtc::SdpParseError err;
    webrtc::IceCandidateInterface* cand =
        webrtc::CreateIceCandidate(mid, mline_index, candidate, &err);
    if (!cand) {
        ROBRT_LOGW("[pull idx=%d] CreateIceCandidate failed: %s mid=%s mline=%d",
                   index_, err.description.c_str(), mid.c_str(), mline_index);
        return;
    }
    bool ok = peer_connection_->AddIceCandidate(cand);
    delete cand;
    if (!ok) {
        ROBRT_LOGW("[pull idx=%d] AddIceCandidate returned false mid=%s mline=%d",
                   index_, mid.c_str(), mline_index);
    }
}

void WebRtcPullStream::FlushPendingRemoteIceCandidates() {
    if (pending_remote_ice_.empty()) return;
    ROBRT_LOGD("[pull idx=%d] flush pending ice, n=%zu",
               index_, pending_remote_ice_.size());
    for (const auto& p : pending_remote_ice_) {
        AddRemoteIceCandidateNow(p.mid, p.mline_index, p.candidate);
    }
    pending_remote_ice_.clear();
}

void WebRtcPullStream::HandleRemoteIceCandidate(const std::string& mid, int mline_index,
                                                 const std::string& candidate) {
    if (!peer_connection_ || !remote_description_applied_) {
        pending_remote_ice_.push_back(PendingRemoteIce{mid, mline_index, candidate});
        ROBRT_LOGD("[pull idx=%d] enqueue remote ice, size=%zu",
                   index_, pending_remote_ice_.size());
        return;
    }
    AddRemoteIceCandidateNow(mid, mline_index, candidate);
}

}  // namespace robrt::client::impl

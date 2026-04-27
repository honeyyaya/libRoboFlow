#include "rtc_stream_session.h"

#include "core/rtc/rtc.h"
#include "signaling/signaling_client.h"

#include "common/internal/frame_impl.h"
#include "common/internal/logger.h"
#include "rflow/librflow_common.h"

#include <algorithm>
#include <condition_variable>
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
#include "api/stats/rtc_stats_collector_callback.h"
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
        receiver->SetJitterBufferMinimumDelay(std::optional<double>(0.0));

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
    return true;
}

void RtcStreamSession::Close() {
    if (closed_.exchange(true, std::memory_order_acq_rel)) return;

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
    config.audio_jitter_buffer_min_delay_ms = 0;

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

}  // namespace rflow::client::impl

#include "rtc_stream_session.h"

#include "rtc/ice_nominated_pair_rtt.h"
#include "rtc/inbound_video_stats_aggregation.h"
#include "rtc/rtc_sync_stats.h"
#include "rtc/rtc.h"
#include "rtc/sdp_observers.h"
#include "rtc/stats_observer.h"
#include "signal/tcp_session.h"
#include "base/timing_log.h"

#include "media/frame_types.h"
#include "public/logger_api.h"
#include "rflow/librflow_common.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_types.h"
#include "api/rtp_parameters.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/rtc_error.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"
#include "api/video_codecs/h264_profile_level_id.h"
#include "rtc_base/thread.h"

namespace rflow::client::impl {

namespace {

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
    result.insert(result.end(), other_h264.begin(), other_h264.end());
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

bool ReadEnvBoolDefaultWd(const char* name, bool def) {
    const char* v = std::getenv(name);
    if (!v || !v[0]) return def;
    if (v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F') return false;
    if (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T') return true;
    return def;
}

int64_t ReadEnvIntWd(const char* name, int64_t def, int64_t lo, int64_t hi) {
    const char* v = std::getenv(name);
    if (!v || !v[0]) return def;
    const int64_t n = std::atoll(v);
    if (n < lo || n > hi) return def;
    return n;
}

int64_t MonoTimeMsWd() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
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
        stream_->jitter_min_delay_seconds_.store(floor_s, std::memory_order_release);
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
    watchdog_enabled_ = ReadEnvBoolDefaultWd("RFLOW_RECEIVER_KEYFRAME_WATCHDOG", true);
    watchdog_stuck_threshold_ms_ =
        ReadEnvIntWd("RFLOW_RECEIVER_WATCHDOG_STUCK_MS", 300, 50, 5000);
    watchdog_cooldown_ms_ =
        ReadEnvIntWd("RFLOW_RECEIVER_WATCHDOG_COOLDOWN_MS", 600, 100, 10000);
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

    signaling_ = std::make_unique<rflow::signal::TcpClientSession>(std::move(config));
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
        if (current_video_track_ && frame_adapter_) {
            current_video_track_->RemoveSink(frame_adapter_.get());
        }
        current_video_track_ = nullptr;
    }
    pending_ice_buffer_.ResetUnapplied();
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
    config.audio_jitter_buffer_max_packets = 1;
    config.audio_jitter_buffer_min_delay_ms = 0;
    config.audio_jitter_buffer_fast_accelerate = true;

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

    pending_ice_buffer_.ResetUnapplied();
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
    pending_set_remote_observer_ = rflow::core::rtc::MakeSetRemoteDescObserver(
        [self](webrtc::RTCError error) {
            self->pending_set_remote_observer_ = nullptr;
            if (!error.ok()) {
                RFLOW_LOGE("[pull idx=%d] SetRemoteDescription failed: %s",
                           self->index_, error.message());
                self->pending_ice_buffer_.ResetUnapplied();
                self->EmitState(RFLOW_STREAM_FAILED, RFLOW_ERR_FAIL);
                return;
            }

            self->FlushPendingRemoteIceCandidates();
            self->DoCreateAnswerAfterSetRemote();
        });

    auto* signaling_thread = peer_connection_->signaling_thread();
    if (!signaling_thread) {
        pending_set_remote_observer_ = nullptr;
        pending_ice_buffer_.ResetUnapplied();
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

    if (factory_) {
        const webrtc::RtpCapabilities caps =
            factory_->GetRtpReceiverCapabilities(webrtc::MediaType::VIDEO);
        std::vector<webrtc::RtpCodecCapability> preferred = BuildLowLatencyVideoCodecPreferences(caps);
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
                       "SetCodecPreferences",
                       index_);
        }
    }

    auto self = shared_from_this();
    pending_create_answer_observer_ = rflow::core::rtc::MakeCreateSdpObserver(
        [self](std::unique_ptr<webrtc::SessionDescriptionInterface> desc) {
            self->pending_create_answer_observer_ = nullptr;
            if (!desc) {
                RFLOW_LOGE("[pull idx=%d] CreateAnswer succeeded with null desc", self->index_);
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

            self->pending_set_local_observer_ = rflow::core::rtc::MakeSetLocalDescObserverLegacy(
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
    auto pending = pending_ice_buffer_.MarkAppliedAndDrain();
    if (pending.empty()) return;

    RFLOW_LOGD("[pull idx=%d] flush pending ice, n=%zu",
               index_, pending.size());
    for (const auto& p : pending) {
        AddRemoteIceCandidateNow(p.mid, p.mline_index, p.candidate);
    }
}

void RtcStreamSession::HandleRemoteIceCandidate(const std::string& mid,
                                                int                mline_index,
                                                const std::string& candidate) {
    if (!peer_connection_ || !pending_ice_buffer_.IsApplied()) {
        const std::size_t pending_count = pending_ice_buffer_.Push(mid, mline_index, candidate);
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

    rflow::core::rtc::InboundVideoRtpAggregation agg;
    uint32_t                   rtt_ms                   = 0;
    uint32_t                   available_incoming_kbps = 0;

    const bool synced = rflow::core::rtc::SyncGetPeerConnectionStats(
        peer_connection_.get(),
        rflow::core::rtc::SyncGetPeerConnectionStatsTimeout(),
        [&](const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
            rflow::core::rtc::AccumulateInboundVideoRtpFromReport(report, &agg);
            rflow::core::rtc::AccumulateIncomingIcePairMetricsFromReport(report,
                                                                         &rtt_ms,
                                                                         &available_incoming_kbps);
        });
    if (!synced) {
        return false;
    }

    out_stats->in_bound_bytes = agg.bytes_received;
    out_stats->in_bound_pkts = agg.packets_received;
    out_stats->lost_pkts = agg.lost_pkts_positive_sum;
    out_stats->fps = agg.fps_uint;
    out_stats->jitter_ms = agg.jitter_ms_max;
    out_stats->freeze_count = agg.freeze_count_max;
    out_stats->decode_fail_count = static_cast<uint32_t>(
        std::min<uint64_t>(agg.frames_dropped_max, static_cast<uint64_t>(UINT32_MAX)));
    out_stats->rtt_ms = rtt_ms;

    out_stats->jitter_buffer_delay_ms =
        (agg.jitter_buffer_emitted_count > 0)
            ? static_cast<uint32_t>(
                  agg.jitter_buffer_delay_seconds * 1000.0 /
                      static_cast<double>(agg.jitter_buffer_emitted_count) +
                  0.5)
            : 0;
    out_stats->jitter_min_delay_ms = static_cast<uint32_t>(
        jitter_min_delay_seconds_.load(std::memory_order_relaxed) * 1000.0 + 0.5);

    {
        const int64_t now_mono_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count();
        std::lock_guard<std::mutex> blk(stats_bitrate_mu_);
        const uint64_t prev_bytes = prev_stats_bytes_received_;
        const int64_t  prev_ms    = prev_stats_collect_mono_ms_;
        uint32_t       kbps       = 0;
        if (prev_ms > 0 && now_mono_ms > prev_ms && agg.bytes_received >= prev_bytes) {
            const uint64_t d_bytes = agg.bytes_received - prev_bytes;
            const int64_t  d_ms    = now_mono_ms - prev_ms;
            kbps = static_cast<uint32_t>((d_bytes * 8ULL) / static_cast<uint64_t>(d_ms));
        } else if (available_incoming_kbps > 0) {
            kbps = available_incoming_kbps;
        }
        prev_stats_bytes_received_  = agg.bytes_received;
        prev_stats_collect_mono_ms_ = now_mono_ms;
        last_bitrate_kbps_          = kbps;
        out_stats->bitrate_kbps     = kbps;
    }
    return true;
}

void RtcStreamSession::StartWatchdogThread() {
    bool expected = false;
    if (!stats_running_.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel)) {
        return;
    }
    prev_frames_decoded_          = 0;
    prev_packets_received_        = 0;
    last_decode_progress_mono_ms_ = MonoTimeMsWd();
    last_keyframe_kick_mono_ms_   = 0;
    last_keyframe_kick_packets_   = 0;
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
    auto cb = rflow::core::rtc::MakeStatsCollectorObserver(
        [self](const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
            self->OnWatchdogStats(report);
        });
    peer_connection_->GetStats(cb.get());
}

void RtcStreamSession::OnWatchdogStats(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
    if (!report || !peer_connection_ || closed_.load(std::memory_order_acquire)) return;

    rflow::core::rtc::InboundVideoRtpAggregation agg;
    rflow::core::rtc::AccumulateInboundVideoRtpFromReport(report, &agg);
    if (!agg.have_video) return;

    const uint64_t frames_decoded      = agg.frames_decoded;
    const uint64_t packets_received    = agg.packets_received;
    const int64_t  packets_lost_signed = agg.packets_lost_signed;
    const uint64_t frames_dropped      = agg.frames_dropped_sum;
    const uint64_t bytes_received      = agg.bytes_received;
    const uint64_t fec_packets         = agg.fec_packets_received;
    const uint32_t nack_count          = agg.nack_count_max;
    const uint32_t pli_count           = agg.pli_count_max;
    const uint32_t fir_count           = agg.fir_count_max;
    const uint32_t frame_w             = agg.frame_width;
    const uint32_t frame_h             = agg.frame_height;
    double         fps                 = agg.fps_double;
    double         total_decode_time_s        = agg.total_decode_time_s;
    double         total_processing_delay_s   = agg.total_processing_delay_s;
    double         total_assembly_time_s      = agg.total_assembly_time_s;
    double         jitter_s                   = agg.jitter_seconds_max;
    double         jb_delay_s                 = agg.jitter_buffer_delay_seconds;
    const uint64_t jb_emitted                 = agg.jitter_buffer_emitted_count;
    std::string    decoder_impl               = agg.decoder_implementation;
    std::string    codec_id                   = agg.codec_id;

    const int64_t now_ms       = MonoTimeMsWd();
    const uint64_t delta_frames =
        (frames_decoded >= prev_frames_decoded_) ? (frames_decoded - prev_frames_decoded_) : 0;

    if (watchdog_enabled_) {
        if (delta_frames > 0) {
            last_decode_progress_mono_ms_ = now_ms;
            last_keyframe_kick_packets_     = packets_received;
        } else if (last_decode_progress_mono_ms_ == 0) {
            last_decode_progress_mono_ms_ = now_ms;
        }

        const int64_t stuck_ms           = now_ms - last_decode_progress_mono_ms_;
        const bool    packets_still_flowing = packets_received > last_keyframe_kick_packets_;
        const bool    cooldown_elapsed =
            last_keyframe_kick_mono_ms_ == 0 ||
            (now_ms - last_keyframe_kick_mono_ms_) >= watchdog_cooldown_ms_;

        if (delta_frames == 0 && packets_still_flowing && stuck_ms >= watchdog_stuck_threshold_ms_ &&
            cooldown_elapsed) {
            RFLOW_LOGW("[pull idx=%d] keyframe watchdog: decoder stalled %lld ms, packets=%llu; "
                       "kicking jitter buffer to force PLI",
                       index_, static_cast<long long>(stuck_ms),
                       static_cast<unsigned long long>(packets_received));
            KickJitterBufferForKeyframe();
            last_keyframe_kick_mono_ms_ = now_ms;
            last_keyframe_kick_packets_ = packets_received;
        }
    }

    if (rflow::timing_log::IsEnabled()) {
        rflow::core::rtc::IceNominatedRttAggregation ice_rtt;
        rflow::core::rtc::AccumulateNominatedIcePairRttFromReport(report, &ice_rtt);
        const double cur_rtt_ms =
            ice_rtt.current_round_trip_time_s * 1000.0;
        const double avg_rtt_ms =
            (ice_rtt.responses_received > 0)
                ? (ice_rtt.total_round_trip_time_s /
                   static_cast<double>(ice_rtt.responses_received)) *
                      1000.0
                : cur_rtt_ms;

        const uint64_t d_decoded =
            stats_log_baseline_done_ && frames_decoded >= prev_frames_decoded_
                ? frames_decoded - prev_frames_decoded_
                : 0;
        const uint64_t d_dropped =
            stats_log_baseline_done_ && frames_dropped >= prev_frames_dropped_
                ? frames_dropped - prev_frames_dropped_
                : 0;
        const double d_decode_time_s =
            stats_log_baseline_done_ && total_decode_time_s >= prev_total_decode_time_s_
                ? total_decode_time_s - prev_total_decode_time_s_
                : 0.0;
        const double d_processing_s =
            stats_log_baseline_done_ && total_processing_delay_s >= prev_total_processing_delay_s_
                ? total_processing_delay_s - prev_total_processing_delay_s_
                : 0.0;
        const double d_assembly_s =
            stats_log_baseline_done_ && total_assembly_time_s >= prev_total_assembly_time_s_
                ? total_assembly_time_s - prev_total_assembly_time_s_
                : 0.0;
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
        const uint64_t pos_lost     = (lost > 0) ? static_cast<uint64_t>(lost) : 0;
        const uint64_t total_for_loss = packets_received + pos_lost;
        const double loss_pct =
            (total_for_loss > 0)
                ? (static_cast<double>(pos_lost) * 100.0 / static_cast<double>(total_for_loss))
                : 0.0;
        const double jitter_ms = jitter_s * 1000.0;

        const char* decoder_cstr = decoder_impl.empty() ? "unknown" : decoder_impl.c_str();

        std::string codec_mime;
        std::string codec_fmtp;
        uint32_t    codec_pt      = 0;
        bool        has_codec_pt  = false;
        if (!codec_id.empty()) {
            if (const auto* codec = report->GetAs<webrtc::RTCCodecStats>(codec_id)) {
                if (codec->mime_type) codec_mime = *codec->mime_type;
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

        RFLOW_LOGD(
            "[Pipeline/Video] %ux%u | decoder=%s | codec=%s | fps=%.1f | "
            "decoded=+%llu total=%llu | dropped=+%llu total=%llu | recv=%llu KB",
            frame_w, frame_h, decoder_cstr, codec_label.c_str(), fps,
            static_cast<unsigned long long>(d_decoded),
            static_cast<unsigned long long>(frames_decoded),
            static_cast<unsigned long long>(d_dropped),
            static_cast<unsigned long long>(frames_dropped),
            static_cast<unsigned long long>(bytes_kb));

        const double jitter_min_ms =
            jitter_min_delay_seconds_.load(std::memory_order_relaxed) * 1000.0;
        RFLOW_LOGD(
            "[Pipeline/Latency] jb_avg=%.1f ms | decode_avg=%.2f ms | "
            "processing_avg=%.2f ms | assembly_avg=%.2f ms | jitter_min=%.1f ms",
            jb_delay_ms, avg_decode_ms, avg_processing_ms, avg_assembly_ms, jitter_min_ms);

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

        RFLOW_LOGD(
            "[Pipeline/Net] rtt=%.1f ms | rtt_avg=%.1f ms | jitter=%.1f ms | "
            "loss=%.2f%% (%lld/%llu) | nack=%u | pli=%u | fir=%u | fec=%llu",
            cur_rtt_ms, avg_rtt_ms, jitter_ms, loss_pct,
            static_cast<long long>(lost),
            static_cast<unsigned long long>(total_for_loss),
            nack_count, pli_count, fir_count,
            static_cast<unsigned long long>(fec_packets));

        prev_frames_dropped_            = frames_dropped;
        prev_total_decode_time_s_       = total_decode_time_s;
        prev_total_processing_delay_s_  = total_processing_delay_s;
        prev_total_assembly_time_s_     = total_assembly_time_s;
        stats_log_baseline_done_       = true;
    }

    prev_frames_decoded_   = frames_decoded;
    prev_packets_received_ = packets_received;
}

void RtcStreamSession::KickJitterBufferForKeyframe() {
    if (!peer_connection_) return;
    const double floor_s = jitter_min_delay_seconds_.load(std::memory_order_acquire);
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


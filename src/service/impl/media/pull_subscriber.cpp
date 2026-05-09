#include "media/pull_subscriber.h"

#include "common/public/log_tagged.h"
#include "core/rtc/peer_connection_factory_deps.h"
#include "core/rtc/sdp_observers.h"
#include "core/rtc/stats_observer.h"
#include "core/runtime/runtime_knobs.h"
#include "media/pull_subscriber_internals.h"
#include "media/pull_subscriber_video_sink.h"
#include "signaling/signaling_client.h"

#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/make_ref_counted.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"
#include "api/set_local_description_observer_interface.h"
#include "api/set_remote_description_observer_interface.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "api/video/video_frame_buffer.h"
#include "libyuv/convert.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/thread.h"
#include "rtc_base/time_utils.h"

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <atomic>
#include <functional>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace rflow::service::impl {

using detail::pull::MakeRtcConfig;
using detail::pull::MediaTimingTraceEnabled;
using detail::pull::MediaTimingTraceEveryN;
using detail::pull::PrintInboundVideoStats;
using detail::pull::SignalingNowUs;
using detail::pull::SignalingTimingTraceEnabled;
using detail::pull::TraceSigTiming;


using observers::VideoSink;

class PullSubscriber::Impl : public webrtc::PeerConnectionObserver {
public:
    Impl(const std::string& url, const std::string& stream_id, const PullSubscriberConfig& recv)
        : signaling_(std::make_unique<SignalingClient>(url, "subscriber", stream_id)),
          recv_config_(recv) {
        stats_.t_construct_us = SignalingNowUs();
    }

    void OnSinkFrame(uint16_t trace_id, int64_t t_callback_done_us) {
        std::lock_guard<std::mutex> lock(mutex_);
        const int64_t now_us = t_callback_done_us > 0 ? t_callback_done_us : SignalingNowUs();
        ++stats_.frames_sink_total;
        stats_.last_frame_us = now_us;
        stats_.last_trace_id = trace_id;
        if (stats_.t_first_frame_us <= 0) {
            stats_.t_first_frame_us = now_us;
            std::ostringstream oss;
            oss << "frames=" << stats_.frames_sink_total << " trace_id=" << static_cast<unsigned>(trace_id);
            if (stats_.t_connected_us > 0 && stats_.t_first_frame_us >= stats_.t_connected_us) {
                oss << " connected_to_first_frame_ms="
                    << (stats_.t_first_frame_us - stats_.t_connected_us) / 1000.0;
            }
            TracePathStat("RX_FIRST_FRAME", oss.str());
        } else if (stats_.frames_sink_total % 120 == 0) {
            std::ostringstream oss;
            oss << "frames=" << stats_.frames_sink_total << " trace_id=" << static_cast<unsigned>(trace_id);
            TracePathStat("RX_FRAME_PROGRESS", oss.str());
        }
    }

    bool Initialize() {
        RFLOW_LOG_TAG_I("PullSubscriber", "Initializing WebRTC (native API)...");
        stats_.t_initialize_begin_us = SignalingNowUs();
        rflow::rtc::EnsureWebrtcFieldTrialsInitialized(); 
        if (!webrtc::InitializeSSL()) {
            return false;
        }
        webrtc::PeerConnectionFactoryDependencies deps;
        rflow::rtc::PeerConnectionFactoryMediaOptions media_opts;
        media_opts.decoder_backend = recv_config_.backend.use_rockchip_mpp_h264_decode
                                         ? rflow::rtc::VideoCodecBackendPreference::kRockchipMpp
                                         : rflow::rtc::VideoCodecBackendPreference::kBuiltin;
        rflow::rtc::ConfigurePeerConnectionFactoryDependencies(deps, &media_opts);
        rflow::rtc::EnsureDedicatedPeerConnectionSignalingThread(deps, &owned_signaling_thread_);
        factory_ = webrtc::CreateModularPeerConnectionFactory(std::move(deps));
        if (!factory_) {
            webrtc::CleanupSSL();
            return false;
        }
        stats_.t_initialize_done_us = SignalingNowUs();
        return CreatePeerConnection();
    }

    void Shutdown() {
        if (video_sink_ && video_track_) {
            video_track_->RemoveSink(video_sink_.get());
        }
        video_sink_.reset();
        video_track_ = nullptr;
        video_rtp_receiver_ = nullptr;
        if (peer_connection_) {
            peer_connection_->Close();
            peer_connection_ = nullptr;
        }
        factory_ = nullptr;
        if (owned_signaling_thread_) {
            owned_signaling_thread_->Stop();
            owned_signaling_thread_.reset();
        }
        TracePathSummary("shutdown");
        webrtc::CleanupSSL();
    }

    bool CreatePeerConnection() {
        auto rtc_config = MakeRtcConfig();
        webrtc::PeerConnectionDependencies deps(this);
        auto result = factory_->CreatePeerConnectionOrError(rtc_config, std::move(deps));
        if (!result.ok()) {
            RFLOW_LOG_TAG_E("PullSubscriber", "CreatePeerConnection failed: %s",
                            result.error().message());
            return false;
        }
        peer_connection_ = result.MoveValue();

        webrtc::RtpTransceiverInit init;
        init.direction = webrtc::RtpTransceiverDirection::kRecvOnly;
        auto tr = peer_connection_->AddTransceiver(webrtc::MediaType::VIDEO, init);
        if (!tr.ok()) {
            RFLOW_LOG_TAG_E("PullSubscriber", "AddTransceiver failed");
            return false;
        }
        RFLOW_LOG_TAG_I("PullSubscriber", "PeerConnection created");
        return true;
    }

    void SetRemoteDescription(const std::string& type, const std::string& sdp) {
        webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc = peer_connection_;
        if (!pc) {
            return;
        }
        webrtc::Thread* sig = pc->signaling_thread();
        if (!sig) {
            ReportPathError("RX_ERR_NO_SIGNALING_THREAD", "SetRemoteDescription: no signaling thread");
            return;
        }
        auto work = [this, pc, type, sdp]() {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stats_.t_set_remote_begin_us = SignalingNowUs();
            }
            TraceSigTiming("SetRemoteDescription begin type=" + type + " sdp_len=" + std::to_string(sdp.size()));
            auto opt_t = webrtc::SdpTypeFromString(type);
            if (!opt_t.has_value()) {
                ReportPathError("RX_ERR_BAD_SDP_TYPE", "bad SDP type");
                return;
            }
            auto desc = webrtc::CreateSessionDescription(*opt_t, sdp);
            if (!desc) {
                ReportPathError("RX_ERR_PARSE_REMOTE_SDP", "parse remote SDP failed");
                return;
            }
            auto obs = rflow::core::rtc::MakeSetRemoteDescObserver([this](webrtc::RTCError err) {
                if (!err.ok()) {
                    ReportPathError("RX_ERR_SET_REMOTE_SDP", std::string("SetRemoteDescription: ") + err.message());
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    stats_.t_set_remote_ok_us = SignalingNowUs();
                }
                TraceSigTiming("SetRemoteDescription OK -> CreateAnswer");
                RFLOW_LOG_TAG_I("PullSubscriber", "SetRemoteDescription OK, CreateAnswer");
                CreateAnswer();
            });
            pc->SetRemoteDescription(std::move(desc), obs);
        };
        if (sig->IsCurrent()) {
            work();
        } else {
            sig->BlockingCall(work);
        }
    }

    void CreateAnswer() {
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions opts;
        opts.offer_to_receive_audio = 0;
        opts.num_simulcast_layers = 1;

        auto obs = rflow::core::rtc::MakeCreateSdpObserver(
                [this](std::unique_ptr<webrtc::SessionDescriptionInterface> desc) {
                    if (!desc) {
                        return;
                    }
                    std::string sdp;
                    if (!desc->ToString(&sdp)) {
                        return;
                    }
                    TraceSigTiming("CreateAnswer success sdp_len=" + std::to_string(sdp.size()));
                    if (rflow::core::runtime::ReadBool("WEBRTC_DUMP_LOCAL_ANSWER")) {
                        std::cout << "\n--- Local answer SDP ---\n" << sdp << "\n--- End ---\n" << std::flush;
                    }
                    auto set_local = rflow::core::rtc::MakeSetLocalDescObserver(
                            [this, sdp](webrtc::RTCError err) {
                                if (!err.ok()) {
                                    ReportPathError("RX_ERR_SET_LOCAL_SDP",
                                                    std::string("SetLocalDescription: ") + err.message());
                                    return;
                                }
                                TraceSigTiming("SetLocalDescription OK (answer)");
                                signaling_->SendAnswer(sdp);
                                {
                                    std::lock_guard<std::mutex> lock(mutex_);
                                    stats_.t_answer_sent_us = SignalingNowUs();
                                }
                                TraceSigTiming("SendAnswer invoked");
                                RFLOW_LOG_TAG_I("PullSubscriber", "Answer sent");
                            });
                    peer_connection_->SetLocalDescription(std::move(desc), set_local);
                },
                [this](webrtc::RTCError err) {
                    ReportPathError("RX_ERR_CREATE_ANSWER", std::string("CreateAnswer: ") + err.message());
                });

        peer_connection_->CreateAnswer(obs.get(), opts);
    }

    void AddRemoteIceCandidate(const std::string& mid, int mline_index, const std::string& candidate) {
        webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc = peer_connection_;
        if (!pc) {
            return;
        }
        webrtc::Thread* sig = pc->signaling_thread();
        if (!sig) {
            return;
        }
        auto work = [this, pc, mid, mline_index, candidate]() {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++stats_.remote_ice_calls;
            }
            TraceSigTiming("AddRemoteIce begin mid=" + mid + " cand_len=" + std::to_string(candidate.size()));
            webrtc::SdpParseError err;
            webrtc::IceCandidateInterface* cand = webrtc::CreateIceCandidate(mid, mline_index, candidate, &err);
            if (!cand) {
                RFLOW_LOG_TAG_E("PullSubscriber", "CreateIceCandidate: %s", err.description.c_str());
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++stats_.remote_ice_parse_fail;
                }
                TracePathStat("RX_ERR_REMOTE_ICE_PARSE", "mid=" + mid + " idx=" + std::to_string(mline_index));
                return;
            }
            std::unique_ptr<webrtc::IceCandidateInterface> owned(cand);
            pc->AddIceCandidate(owned.get());
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++stats_.remote_ice_added;
            }
        };
        if (sig->IsCurrent()) {
            work();
        } else {
            sig->BlockingCall(work);
        }
    }

    void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override {
        AttachVideo(transceiver ? transceiver->receiver() : nullptr);
    }

    void OnAddTrack(webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
                    const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>&) override {
        AttachVideo(receiver);
    }

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override {}

    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override {
        if (!candidate || !signaling_) {
            return;
        }
        std::string sdp;
        if (!candidate->ToString(&sdp)) {
            return;
        }
        std::cout << "[PullSubscriber] Send ICE candidate mid=" << candidate->sdp_mid() << std::endl;
        signaling_->SendIceCandidate(candidate->sdp_mid(), candidate->sdp_mline_index(), sdp);
    }

    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState state) override {
        if (!on_connection_state_) {
            return;
        }
        const int64_t now_us = SignalingNowUs();
        PullConnectionState cs = PullConnectionState::New;
        switch (state) {
            case webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting:
                cs = PullConnectionState::Connecting;
                break;
            case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:
                cs = PullConnectionState::Connected;
                break;
            case webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected:
                cs = PullConnectionState::Disconnected;
                break;
            case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed:
                cs = PullConnectionState::Failed;
                break;
            case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:
                cs = PullConnectionState::Closed;
                break;
            default:
                break;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.pc_state_changes;
            if (cs == PullConnectionState::Connected && stats_.t_connected_us <= 0) {
                stats_.t_connected_us = now_us;
                TracePathStat("RX_CONN_CONNECTED", "state=connected");
            }
            if ((cs == PullConnectionState::Disconnected || cs == PullConnectionState::Failed) &&
                stats_.t_connected_us > 0 && stats_.frames_sink_total == 0) {
                std::ostringstream oss;
                oss << "state=" << (cs == PullConnectionState::Failed ? "failed" : "disconnected")
                    << " connected_without_frame_ms=" << (now_us - stats_.t_connected_us) / 1000.0;
                TracePathStat("RX_ERR_NO_FRAME_WHILE_CONNECTED", oss.str());
            }
            if ((cs == PullConnectionState::Disconnected || cs == PullConnectionState::Failed) &&
                stats_.last_frame_us > 0) {
                std::ostringstream oss;
                oss << "state=" << (cs == PullConnectionState::Failed ? "failed" : "disconnected")
                    << " since_last_frame_ms=" << (now_us - stats_.last_frame_us) / 1000.0
                    << " frames=" << stats_.frames_sink_total;
                TracePathStat("RX_CONN_FRAME_GAP", oss.str());
            }
        }
        on_connection_state_(cs);
    }

    void RequestInboundVideoStatsLog() {
        webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc = peer_connection_;
        webrtc::scoped_refptr<webrtc::RtpReceiverInterface> recv = video_rtp_receiver_;
        if (!pc) {
            return;
        }
        webrtc::Thread* sig = pc->signaling_thread();
        if (!sig) {
            return;
        }
        auto work = [pc, recv]() {
            auto cb = rflow::core::rtc::MakeStatsCollectorObserver(
                [](const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
                    PrintInboundVideoStats(report);
                });
            if (recv) {
                pc->GetStats(recv, cb);
            } else {
                pc->GetStats(cb.get());
            }
        };
        if (sig->IsCurrent()) {
            work();
        } else {
            sig->BlockingCall(work);
        }
    }

    std::unique_ptr<SignalingClient> signaling_;
    PullSubscriberConfig recv_config_;
    std::unique_ptr<webrtc::Thread> owned_signaling_thread_;
    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
    webrtc::scoped_refptr<webrtc::RtpReceiverInterface> video_rtp_receiver_;
    std::shared_ptr<VideoSink> video_sink_;
    std::mutex mutex_;
    OnConnectionStateCallback on_connection_state_;
    OnErrorCallback on_error_;

private:
    struct RxPathStats {
        int64_t t_construct_us{0};
        int64_t t_initialize_begin_us{0};
        int64_t t_initialize_done_us{0};
        int64_t t_set_remote_begin_us{0};
        int64_t t_set_remote_ok_us{0};
        int64_t t_answer_sent_us{0};
        int64_t t_connected_us{0};
        int64_t t_first_frame_us{0};
        int64_t last_frame_us{0};
        uint16_t last_trace_id{0};
        uint64_t attach_calls{0};
        uint64_t attach_dedup{0};
        uint64_t attach_effective{0};
        uint64_t remote_ice_calls{0};
        uint64_t remote_ice_parse_fail{0};
        uint64_t remote_ice_added{0};
        uint64_t pc_state_changes{0};
        uint64_t frames_sink_total{0};
        uint64_t error_reports{0};
    };
    RxPathStats stats_{};

    void TracePathStat(const char* code, const std::string& details) const {
        std::cout << "[PATH_STAT][rx] t_us=" << SignalingNowUs() << " code=" << code;
        if (!details.empty()) {
            std::cout << " " << details;
        }
        std::cout << std::endl;
    }

    void ReportPathError(const char* code, const std::string& msg) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.error_reports;
        }
        TracePathStat(code, "msg=\"" + msg + "\"");
        if (on_error_) {
            on_error_(msg);
        }
    }

    void TracePathSummary(const char* reason) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "reason=" << reason << " frames=" << stats_.frames_sink_total << " attach_calls=" << stats_.attach_calls
            << " attach_dedup=" << stats_.attach_dedup << " attach_effective=" << stats_.attach_effective
            << " remote_ice_calls=" << stats_.remote_ice_calls
            << " remote_ice_parse_fail=" << stats_.remote_ice_parse_fail
            << " remote_ice_added=" << stats_.remote_ice_added << " state_changes=" << stats_.pc_state_changes
            << " errors=" << stats_.error_reports;
        if (stats_.t_connected_us > 0) {
            oss << " connected_at_us=" << stats_.t_connected_us;
        }
        if (stats_.t_first_frame_us > 0) {
            oss << " first_frame_at_us=" << stats_.t_first_frame_us;
        }
        if (stats_.t_connected_us > 0 && stats_.t_first_frame_us > 0 &&
            stats_.t_first_frame_us >= stats_.t_connected_us) {
            oss << " connected_to_first_frame_ms=" << (stats_.t_first_frame_us - stats_.t_connected_us) / 1000.0;
        }
        if (stats_.last_frame_us > 0) {
            oss << " last_frame_us=" << stats_.last_frame_us << " last_trace_id=" << stats_.last_trace_id;
        }
        TracePathStat("RX_PATH_SUMMARY", oss.str());
    }

    void AttachVideo(webrtc::scoped_refptr<webrtc::RtpReceiverInterface> r) {
        if (!r || !video_sink_) {
            return;
        }
        auto track = r->track();
        if (!track || track->kind() != "video") {
            return;
        }
        auto* vt = static_cast<webrtc::VideoTrackInterface*>(track.get());
        if (!vt) {
            return;
        }
        if (recv_config_.common.jitter_buffer_min_delay_ms >= 0) {
            r->SetJitterBufferMinimumDelay(
                std::optional<double>(static_cast<double>(recv_config_.common.jitter_buffer_min_delay_ms) / 1000.0));
        }
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.attach_calls;
        const bool same_track = (video_track_.get() == vt);
        const bool same_receiver = (video_rtp_receiver_.get() == r.get());
        if (same_track && same_receiver) {
            ++stats_.attach_dedup;
            return;
        }
        if (video_track_ && !same_track) {
            video_track_->RemoveSink(video_sink_.get());
        }
        video_track_ = vt;
        video_rtp_receiver_ = std::move(r);
        if (!same_track) {
            ++stats_.attach_effective;
            video_track_->AddOrUpdateSink(video_sink_.get(), webrtc::VideoSinkWants());
            std::cout << "[PullSubscriber] Video track attached" << std::endl;
            TracePathStat("RX_ATTACH_VIDEO", "attach_effective=" + std::to_string(stats_.attach_effective));
        }
    }
};

PullSubscriber::PullSubscriber(const std::string& signaling_url,
                               const std::string& stream_id,
                               const PullSubscriberConfig& recv)
    : impl_(std::make_unique<Impl>(signaling_url, stream_id, recv)) {}

PullSubscriber::~PullSubscriber() {
    Stop();
}

void PullSubscriber::Play() {
    if (is_playing_) {
        return;
    }

    impl_->on_connection_state_ = on_connection_state_;
    impl_->on_error_ = on_error_;
    impl_->video_sink_ = std::make_shared<VideoSink>(
        on_video_frame_, impl_->recv_config_.common.skip_sink_argb_conversion,
        [impl = impl_.get()](uint16_t trace_id, int64_t t_callback_done_us) {
            if (impl) {
                impl->OnSinkFrame(trace_id, t_callback_done_us);
            }
        });
    if (impl_->recv_config_.common.skip_sink_argb_conversion) {
        std::cout << "[PullSubscriber] VideoSink: skip I420→ARGB (client low-latency path)" << std::endl;
    }

    impl_->signaling_->SetOnOffer([this](const std::string& peer_id, const std::string& type,
                                         const std::string& sdp) {
        std::cout << "[PullSubscriber] Received offer from=" << peer_id << " (type=" << type << ", len=" << sdp.size() << ")"
                  << std::endl;
        if (rflow::core::runtime::ReadBool("WEBRTC_DUMP_REMOTE_OFFER")) {
            std::cout << "\n--- Remote offer SDP ---\n" << sdp << "\n--- End ---\n" << std::flush;
        }
        impl_->SetRemoteDescription(type, sdp);
    });
    impl_->signaling_->SetOnIce([this](const std::string& peer_id, const std::string& mid, int mline_index,
                                       const std::string& candidate) {
        std::cout << "[PullSubscriber] ICE from=" << peer_id << " mid=" << mid << " idx=" << mline_index << std::endl;
        impl_->AddRemoteIceCandidate(mid, mline_index, candidate);
    });
    impl_->signaling_->SetOnError([this](const std::string& msg) {
        if (on_error_) {
            on_error_(msg);
        }
    });

    if (!impl_->Initialize()) {
        impl_->signaling_->Stop();
        impl_->Shutdown();
        if (on_error_) {
            on_error_("WebRTC init failed");
        }
        return;
    }
    if (!impl_->signaling_->Start()) {
        impl_->signaling_->Stop();
        impl_->Shutdown();
        if (on_error_) {
            on_error_("Signaling failed. Run: ./build/bin/signaling_server");
        }
        return;
    }
    is_playing_ = true;
    std::cout << "[PullSubscriber] Waiting for offer from publisher..." << std::endl;
}

void PullSubscriber::Stop() {
    if (!is_playing_) {
        return;
    }
    impl_->signaling_->Stop();
    impl_->Shutdown();
    is_playing_ = false;
}

void PullSubscriber::RequestInboundVideoStatsLog() {
    if (impl_) {
        impl_->RequestInboundVideoStatsLog();
    }
}

}  // namespace rflow::service::impl

#include "publish/publisher.h"

#include <cctype>
#include <utility>

#include "media/capture_fps_pipeline_policy.h"
#include "media/push_streamer.h"
#include "signaling/signaling_client.h"

#include "public/logger_api.h"
#include "runtime/runtime_knobs.h"

namespace rflow::service::impl {

namespace policy = rflow::service::impl::policy;

namespace {

std::string LowerCopy(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

Publisher::Publisher(int32_t stream_idx,
                     rflow_codec_t in_codec,
                     const std::string& stream_id_str,
                     const std::string& signaling_url,
                     const std::string& device_id,
                     int width, int height, int fps,
                     int target_kbps, int min_kbps, int max_kbps,
                     const std::string& video_codec,
                     bool use_internal_video_source,
                     const std::string& video_device_path,
                     int video_device_index,
                     std::optional<std::string> degradation_pref_override,
                     PublisherMediaOptions          media_opts,
                     const PublisherPullCallbacks& cbs)
    : stream_idx_(stream_idx),
      in_codec_(in_codec),
      stream_id_str_(stream_id_str),
      signaling_url_(signaling_url),
      device_id_(device_id),
      width_(width > 0 ? width : 1280),
      height_(height > 0 ? height : 720),
      fps_(fps > 0 ? fps : 30),
      target_kbps_(target_kbps > 0 ? target_kbps : 1000),
      min_kbps_(min_kbps > 0 ? min_kbps : 100),
      max_kbps_(max_kbps > 0 ? max_kbps : 2000),
      video_codec_(video_codec.empty() ? std::string("h264") : LowerCopy(video_codec)),
      use_internal_video_source_(use_internal_video_source),
      video_device_path_(video_device_path),
      video_device_index_(video_device_index >= 0 ? video_device_index : 0),
      degradation_pref_override_(std::move(degradation_pref_override)),
      media_opts_(std::move(media_opts)),
      cbs_(cbs) {}

Publisher::~Publisher() {
    Stop();
}

bool Publisher::Start() {
    if (streamer_) {
        return true;  // 幂等
    }

    PushStreamerConfig cfg;
    cfg.common.stream_id                     = stream_id_str_;
    cfg.common.video_width                   = width_;
    cfg.common.video_height                  = height_;
    cfg.common.video_fps                     = fps_;
    cfg.common.use_external_video_source     = !use_internal_video_source_;
    cfg.common.video_device_path             = video_device_path_;
    cfg.common.video_device_index            = video_device_index_;
    cfg.common.signaling_subscriber_offer_only = true;  // 仅在 subscriber_join 时创建 offer
    cfg.common.target_bitrate_kbps           = target_kbps_;
    cfg.common.min_bitrate_kbps              = min_kbps_;
    cfg.common.max_bitrate_kbps              = max_kbps_;
    cfg.common.video_codec = video_codec_;
    cfg.common.bitrate_mode = "vbr";
    if (media_opts_.bitrate_mode.has_value()) {
        const bool want_cbr = (*media_opts_.bitrate_mode == RFLOW_BITRATE_MODE_CBR);
        cfg.common.bitrate_mode = want_cbr ? "cbr" : "vbr";
        if (want_cbr) {
            cfg.common.min_bitrate_kbps = cfg.common.target_bitrate_kbps;
            cfg.common.max_bitrate_kbps = cfg.common.target_bitrate_kbps;
        }
    } else if (min_kbps_ == max_kbps_) {
        cfg.common.bitrate_mode     = "cbr";
        cfg.common.min_bitrate_kbps = cfg.common.target_bitrate_kbps;
        cfg.common.max_bitrate_kbps = cfg.common.target_bitrate_kbps;
    }
    if (degradation_pref_override_) {
        cfg.common.degradation_preference = LowerCopy(*degradation_pref_override_);
    }

    if (media_opts_.h264_profile) cfg.common.h264_profile = LowerCopy(*media_opts_.h264_profile);
    if (media_opts_.h264_level) cfg.common.h264_level = LowerCopy(*media_opts_.h264_level);
    if (media_opts_.keyframe_gop_frames.has_value()) {
        cfg.common.keyframe_interval = *media_opts_.keyframe_gop_frames;
    }
    if (media_opts_.ice_prioritize_likely_pairs.has_value()) {
        cfg.common.ice_prioritize_likely_pairs = *media_opts_.ice_prioritize_likely_pairs;
    }
    if (media_opts_.stun_server.has_value()) {
        cfg.common.stun_server     = *media_opts_.stun_server;
        cfg.common.has_stun_server = true;
    }
    if (media_opts_.turn_server.has_value()) {
        cfg.common.turn_server     = *media_opts_.turn_server;
        cfg.common.turn_username   = media_opts_.turn_username.value_or(std::string{});
        cfg.common.turn_password   = media_opts_.turn_password.value_or(std::string{});
        cfg.common.has_turn_server = true;
    }
    if (media_opts_.video_network_priority) {
        cfg.common.video_network_priority = LowerCopy(*media_opts_.video_network_priority);
    }
    if (media_opts_.video_encoding_max_framerate.has_value()) {
        cfg.common.video_encoding_max_framerate = *media_opts_.video_encoding_max_framerate;
    }
    if (media_opts_.capture_warmup_sec.has_value()) {
        cfg.common.capture_warmup_sec = *media_opts_.capture_warmup_sec;
    }
    if (media_opts_.capture_gate_min_frames.has_value()) {
        cfg.common.capture_gate_min_frames = *media_opts_.capture_gate_min_frames;
    }
    if (media_opts_.capture_gate_max_wait_sec.has_value()) {
        cfg.common.capture_gate_max_wait_sec = *media_opts_.capture_gate_max_wait_sec;
    }

    // Rockchip MPP 硬件编解码：仅 RFLOW_ENABLE_ROCKCHIP_MPP=ON 时默认启用。
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
    cfg.backend.use_rockchip_mpp_h264 = true;
    cfg.backend.use_rockchip_mpp_mjpeg_decode = true;
#else
    cfg.backend.use_rockchip_mpp_h264 = false;
    cfg.backend.use_rockchip_mpp_mjpeg_decode = false;
#endif
    policy::ApplyCaptureFpsPipelineDefaults(cfg.backend, cfg.common.video_fps);

    streamer_ = std::make_unique<PushStreamer>(cfg);

    signaling_ = std::make_unique<SignalingClient>(
        signaling_url_, "publisher", stream_id_str_);

    // signaling IO 线程 → Publisher worker 线程：排队主线程统一 CreateOfferForPeer，
    // 避免 CreatePeerConnection 被 IO 线程 block（与 push_demo 对齐）。
    signaling_->SetOnAnswer([this](const std::string& peer_id, const std::string& type,
                                    const std::string& sdp) {
        if (streamer_) streamer_->SetRemoteDescriptionForPeer(peer_id, type, sdp);
    });
    signaling_->SetOnIce([this](const std::string& peer_id, const std::string& mid,
                                 int mline_index, const std::string& candidate) {
        if (streamer_) streamer_->AddRemoteIceCandidateForPeer(peer_id, mid, mline_index, candidate);
    });
    signaling_->SetOnSubscriberJoin([this](const std::string& peer_id) {
        offer_pump_.Enqueue(peer_id);
        if (cbs_.on_pull_request) {
            cbs_.on_pull_request(stream_idx_, cbs_.userdata);
        }
    });
    signaling_->SetOnSubscriberLeave([this](const std::string& peer_id) {
        if (streamer_) {
            streamer_->ClosePeerForSubscriber(peer_id);
        }
        if (cbs_.on_pull_release) {
            cbs_.on_pull_release(stream_idx_, cbs_.userdata);
        }
    });
    signaling_->SetOnError([this](const std::string& msg) {
        RFLOW_LOGE("[publisher] signaling error: %s", msg.c_str());
        if (cbs_.on_connect_state) {
            cbs_.on_connect_state(RFLOW_CONN_DISCONNECTED, RFLOW_OK, cbs_.userdata);
        }
    });

    streamer_->SetOnSdpCallback([this](const std::string& peer_id, const std::string& type,
                                        const std::string& sdp) {
        if (!signaling_ || type != "offer") return;
        signaling_->SendOffer(sdp, peer_id);
    });
    streamer_->SetOnIceCandidateCallback([this](const std::string& peer_id, const std::string& mid,
                                                 int mline_index, const std::string& candidate) {
        if (signaling_) signaling_->SendIceCandidate(mid, mline_index, candidate, peer_id);
    });
    streamer_->SetOnConnectionStateCallback([this](ConnectionState state) {
        const char* names[] = {"New", "Connecting", "Connected", "Disconnected", "Failed", "Closed"};
        RFLOW_LOGI("[publisher idx=%d] rtc state=%s", stream_idx_, names[static_cast<int>(state)]);
    });

    if (!signaling_->Start()) {
        RFLOW_LOGE("[publisher idx=%d] signaling start failed url=%s",
                   stream_idx_, signaling_url_.c_str());
        signaling_.reset();
        streamer_.reset();
        return false;
    }

    if (!streamer_->Start()) {
        RFLOW_LOGE("[publisher idx=%d] push streamer start failed", stream_idx_);
        signaling_->Stop();
        signaling_.reset();
        streamer_.reset();
        return false;
    }

    offer_pump_.Start([this](const std::string& peer_id) {
        if (streamer_) {
            streamer_->CreateOfferForPeer(peer_id);
        }
    });
    RFLOW_LOGI("[publisher idx=%d] started (signaling=%s, stream_id=%s, %dx%d@%d, codec=%s, source=%s)",
               stream_idx_, signaling_url_.c_str(), stream_id_str_.c_str(),
               width_, height_, fps_, video_codec_.c_str(),
               use_internal_video_source_ ? "sdk-camera" : "external-push");
    if (use_internal_video_source_) {
        if (!video_device_path_.empty()) {
            RFLOW_LOGI("[publisher idx=%d] camera path=%s", stream_idx_, video_device_path_.c_str());
        } else {
            RFLOW_LOGI("[publisher idx=%d] camera index=%d", stream_idx_, video_device_index_);
        }
    }
    return true;
}

void Publisher::Stop() {
    offer_pump_.Stop();
    if (streamer_) {
        streamer_->Stop();
        streamer_.reset();
    }
    if (signaling_) {
        signaling_->Stop();
        signaling_.reset();
    }
}

bool Publisher::PushI420(const uint8_t* buf, uint32_t size, int w, int h, int64_t ts_us) {
    if (!streamer_) return false;
    if (streamer_->PushExternalI420Contiguous(buf, size, w, h, ts_us)) {
        video_frames_pushed_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

bool Publisher::PushNv12(const uint8_t* buf, uint32_t size, int w, int h, int64_t ts_us) {
    if (!streamer_) return false;
    if (streamer_->PushExternalNv12Contiguous(buf, size, w, h, ts_us)) {
        video_frames_pushed_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

bool Publisher::CollectStats(librflow_stream_stats_s* out_stats) {
    if (!streamer_ || !out_stats) return false;
    return streamer_->CollectStats(out_stats);
}

}  // namespace rflow::service::impl

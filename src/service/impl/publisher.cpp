#include "publisher.h"

#include <cctype>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <utility>

#include "media/push_streamer.h"
#include "signaling/signaling_client.h"

#include "common/internal/logger.h"

namespace rflow::service::impl {

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
    cfg.common.video_codec                   = video_codec_;
    if (min_kbps_ == max_kbps_) {
        cfg.common.bitrate_mode = "cbr";
    }
    cfg.common.degradation_preference = "maintain_framerate";
    if (const char* deg = std::getenv("RFLOW_SVC_DEGRADATION_PREFERENCE")) {
        if (deg[0] != '\0') {
            cfg.common.degradation_preference = deg;
        }
    }

    // Rockchip MPP 硬件编解码：根据编译宏默认打开；运行时再由 WEBRTC_MPP_* 等环境变量二次控制。
    cfg.backend.use_rockchip_mpp_h264 = true;

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
        {
            std::lock_guard<std::mutex> lk(pending_mu_);
            pending_subs_.push_back(peer_id);
        }
        pending_cv_.notify_one();
        if (cbs_.on_pull_request) {
            cbs_.on_pull_request(stream_idx_, cbs_.userdata);
        }
    });
    signaling_->SetOnSubscriberLeave([this](const std::string& /*peer_id*/) {
        if (cbs_.on_pull_release) {
            cbs_.on_pull_release(stream_idx_, cbs_.userdata);
        }
    });
    signaling_->SetOnError([](const std::string& msg) {
        RFLOW_LOGE("[publisher] signaling error: %s", msg.c_str());
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
        using S = ConnectionState;
        const char* names[] = {"New", "Connecting", "Connected", "Disconnected", "Failed", "Closed"};
        RFLOW_LOGI("[publisher idx=%d] rtc state=%s", stream_idx_, names[static_cast<int>(state)]);
        (void)state;
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

    worker_run_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { WorkerLoop(); });
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
    if (worker_run_.exchange(false, std::memory_order_acq_rel)) {
        pending_cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    if (streamer_) {
        streamer_->Stop();
        streamer_.reset();
    }
    if (signaling_) {
        signaling_->Stop();
        signaling_.reset();
    }
    std::lock_guard<std::mutex> lk(pending_mu_);
    pending_subs_.clear();
}

void Publisher::WorkerLoop() {
    while (worker_run_.load(std::memory_order_acquire)) {
        std::vector<std::string> batch;
        {
            std::unique_lock<std::mutex> lk(pending_mu_);
            pending_cv_.wait_for(lk, std::chrono::seconds(1), [this] {
                return !worker_run_.load(std::memory_order_acquire) || !pending_subs_.empty();
            });
            if (!worker_run_.load(std::memory_order_acquire)) return;
            batch.swap(pending_subs_);
        }
        if (!streamer_) continue;
        for (const auto& peer_id : batch) {
            streamer_->CreateOfferForPeer(peer_id);
        }
    }
}

bool Publisher::PushI420(const uint8_t* buf, uint32_t size, int w, int h, int64_t ts_us) {
    if (!streamer_) return false;
    return streamer_->PushExternalI420Contiguous(buf, size, w, h, ts_us);
}

bool Publisher::PushNv12(const uint8_t* buf, uint32_t size, int w, int h, int64_t ts_us) {
    if (!streamer_) return false;
    return streamer_->PushExternalNv12Contiguous(buf, size, w, h, ts_us);
}

}  // namespace rflow::service::impl

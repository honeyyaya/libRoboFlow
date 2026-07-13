#include "common/demo_push_session.h"

#include "common/demo_helpers.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace rflow::apps::common {

PushDemoSession::PushDemoSession(PushDemoConfig config) : config_(std::move(config)) {
    if (const char* env_max = std::getenv("RFLOW_PUSH_DEMO_MAX_FAILURES")) {
        max_failures_exit_ = std::max(0, std::atoi(env_max));
    }
#if defined(__linux__)
    CameraHotplugConfig cam_cfg;
    cam_cfg.dev_path = config_.camera;
    camera_          = std::make_unique<CameraHotplugMonitor>(cam_cfg);
    camera_->SetOnDevPathChanged([this](const std::string& new_path) {
        config_.camera = new_path;
        want_stream_restart_.store(true);
    });
#endif
}

void PushDemoSession::OnConnectState(rflow_connect_state_t state, rflow_err_t reason, void* ud) {
    auto* self = static_cast<PushDemoSession*>(ud);
    if (self) {
        self->HandleConnectState(state, reason);
    }
    LogConnectState(state, reason);
}

void PushDemoSession::OnBindState(rflow_bind_state_t state, const char* /*detail*/, void* /*ud*/) {
    std::cout << "[demo] bind state=" << state << std::endl;
}

void PushDemoSession::OnPullRequest(rflow_stream_index_t idx, void* /*ud*/) {
    std::cout << "[demo] on_pull_request idx=" << idx << std::endl;
}

void PushDemoSession::OnPullRelease(rflow_stream_index_t idx, void* /*ud*/) {
    std::cout << "[demo] on_pull_release idx=" << idx << std::endl;
}

void PushDemoSession::OnStreamState(librflow_svc_stream_handle_t /*h*/, rflow_stream_state_t state,
                                    rflow_err_t reason, void* ud) {
    auto* self = static_cast<PushDemoSession*>(ud);
    if (self) {
        self->HandleStreamState(state, reason);
    }
    LogStreamState(state, reason);
}

void PushDemoSession::HandleConnectState(rflow_connect_state_t state, rflow_err_t /*reason*/) {
    conn_state_.store(state);
    if (state == RFLOW_CONN_CONNECTED) {
        want_connect_restart_.store(false);
        consecutive_connect_failures_ = 0;
    } else if (state == RFLOW_CONN_DISCONNECTED || state == RFLOW_CONN_FAILED) {
        want_connect_restart_.store(true);
        want_stream_restart_.store(true);
    }
}

void PushDemoSession::CheckFailureExitThreshold() {
    if (max_failures_exit_ <= 0) return;
    const int n = std::max(consecutive_stream_failures_, consecutive_connect_failures_);
    if (n < max_failures_exit_) return;
    std::cerr << "[demo] fatal: failures reached " << n << " (RFLOW_PUSH_DEMO_MAX_FAILURES="
              << max_failures_exit_ << "), stopping" << std::endl;
    exit_due_to_failures_ = true;
    RequestDemoStop();
}

void PushDemoSession::NoteStreamStartFailure() {
    ++consecutive_stream_failures_;
    if (consecutive_stream_failures_ % kWarnEveryFailures == 0) {
        std::cerr << "[demo] warning: stream start failed " << consecutive_stream_failures_
                  << " times (check camera / signaling)" << std::endl;
    }
    CheckFailureExitThreshold();
}

void PushDemoSession::NoteConnectFailure() {
    ++consecutive_connect_failures_;
    if (consecutive_connect_failures_ % kWarnEveryFailures == 0) {
        std::cerr << "[demo] warning: signaling reconnect failed "
                  << consecutive_connect_failures_ << " times" << std::endl;
    }
    CheckFailureExitThreshold();
}

bool PushDemoSession::AttemptSignalingConnect() {
    auto info = librflow_svc_connect_info_create();
    FillConnectInfo(info, config_);

    auto ccb = librflow_svc_connect_cb_create();
    RegisterConnectCallbacks(ccb);

    const rflow_err_t rc = librflow_svc_connect(info, ccb);
    librflow_svc_connect_info_destroy(info);
    librflow_svc_connect_cb_destroy(ccb);
    return rc == RFLOW_OK && conn_state_.load() == RFLOW_CONN_CONNECTED;
}

void PushDemoSession::HandleStreamState(rflow_stream_state_t state, rflow_err_t /*reason*/) {
    if (!shutting_down_.load() &&
        (state == RFLOW_STREAM_FAILED ||
         (state == RFLOW_STREAM_CLOSED && stream_active_.load()))) {
        want_stream_restart_.store(true);
    }
    if (state == RFLOW_STREAM_IDLE || state == RFLOW_STREAM_CLOSED) {
        stream_active_.store(false);
    } else if (state == RFLOW_STREAM_OPENED) {
        stream_active_.store(true);
    }
}

void PushDemoSession::RegisterConnectCallbacks(librflow_svc_connect_cb_t cb) {
    librflow_svc_connect_cb_set_on_state(cb, OnConnectState);
    librflow_svc_connect_cb_set_on_bind_state(cb, OnBindState);
    librflow_svc_connect_cb_set_on_pull_request(cb, OnPullRequest);
    librflow_svc_connect_cb_set_on_pull_release(cb, OnPullRelease);
    librflow_svc_connect_cb_set_userdata(cb, this);
}

void PushDemoSession::FillConnectInfo(librflow_svc_connect_info_t info, const PushDemoConfig& cfg) {
    librflow_svc_connect_info_set_device_id(info, cfg.device_id.c_str());
    librflow_svc_connect_info_set_device_secret(info, "");
    librflow_svc_connect_info_set_product_key(info, "rflow_demo");
    librflow_svc_connect_info_set_vendor_id(info, "rflow");
}

bool PushDemoSession::Connect() {
    if (!AttemptSignalingConnect()) {
        std::cerr << "[demo] svc_connect failed\n";
        return false;
    }
    return true;
}

bool PushDemoSession::TryReconnectSignaling(std::chrono::steady_clock::time_point now) {
    if (conn_state_.load() == RFLOW_CONN_CONNECTED) {
        want_connect_restart_.store(false);
        return true;
    }
    if (now < next_connect_attempt_) {
        return false;
    }

    if (AttemptSignalingConnect()) {
        std::cout << "[demo] signaling reconnected" << std::endl;
        consecutive_connect_failures_ = 0;
        want_connect_restart_.store(false);
        return true;
    }

    // SDK 在 kConnected 时拒绝再次 connect；仅在首次失败后 disconnect 再试。
    librflow_svc_disconnect();
    conn_state_.store(RFLOW_CONN_IDLE);

    if (AttemptSignalingConnect()) {
        std::cout << "[demo] signaling reconnected (after disconnect)" << std::endl;
        consecutive_connect_failures_ = 0;
        want_connect_restart_.store(false);
        return true;
    }

    std::cerr << "[demo] svc_connect retry failed\n";
    NoteConnectFailure();
    next_connect_attempt_ = now + std::chrono::milliseconds(kConnectRestartBackoffMs);
    return false;
}

librflow_svc_stream_param_t PushDemoSession::MakeStreamParam() const {
    auto sp = librflow_svc_stream_param_create();
    librflow_svc_stream_param_set_out_codec(sp, RFLOW_CODEC_H264);
    librflow_svc_stream_param_set_src_size(sp, static_cast<uint32_t>(config_.width),
                                           static_cast<uint32_t>(config_.height));
    librflow_svc_stream_param_set_out_size(sp, static_cast<uint32_t>(config_.width),
                                           static_cast<uint32_t>(config_.height));
    librflow_svc_stream_param_set_fps(sp, static_cast<uint32_t>(config_.fps));
    librflow_svc_stream_param_set_bitrate(sp, 1000, 2500);
    librflow_svc_stream_param_set_bitrate_mode(sp, RFLOW_BITRATE_MODE_VBR);
    librflow_svc_stream_param_set_degradation_preference(sp, RFLOW_DEGRADATION_MAINTAIN_FRAMERATE);
    librflow_svc_stream_param_set_h264_profile(sp, "main");
    librflow_svc_stream_param_set_h264_level(sp, "4.2");
    librflow_svc_stream_param_set_gop(sp, 30);
    librflow_svc_stream_param_set_ice_prioritize_likely_pairs(sp, true);
    librflow_svc_stream_param_set_video_network_priority(sp, RFLOW_SVC_NETWORK_PRIORITY_HIGH);
#if defined(__linux__)
    librflow_svc_stream_param_set_video_device_path(sp, config_.camera.c_str());
#else
    librflow_svc_stream_param_set_video_device_index(sp, 0);
#endif
    return sp;
}

void PushDemoSession::TeardownStream() {
    if (!stream_) return;
    librflow_svc_stream_handle_t h = stream_;
    stream_                        = nullptr;
    stream_active_.store(false);
    std::cout << "[demo] tearing down stream (camera disconnect or stream failure)" << std::endl;
    librflow_svc_stop_stream(h);
    librflow_svc_destroy_stream(h);
    want_stream_restart_.store(true);
    next_stream_attempt_ =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kStreamRestartBackoffMs);
}

bool PushDemoSession::TryStartStream() {
    if (stream_) return true;
    if (conn_state_.load() != RFLOW_CONN_CONNECTED) return false;

    auto sp = MakeStreamParam();
    auto scb = librflow_svc_stream_cb_create();
    librflow_svc_stream_cb_set_on_state(scb, OnStreamState);
    librflow_svc_stream_cb_set_userdata(scb, this);

    librflow_svc_stream_handle_t stream = nullptr;
    const rflow_err_t create_rc =
        librflow_svc_create_stream(config_.stream_idx, sp, scb, &stream);
    librflow_svc_stream_param_destroy(sp);
    librflow_svc_stream_cb_destroy(scb);
    if (create_rc != RFLOW_OK) {
        std::cerr << "[demo] svc_create_stream failed on reconnect\n";
        want_stream_restart_.store(true);
        NoteStreamStartFailure();
        next_stream_attempt_ =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kStreamRestartBackoffMs);
        return false;
    }

    if (librflow_svc_start_stream(stream) != RFLOW_OK) {
        std::cerr << "[demo] svc_start_stream failed on reconnect\n";
        librflow_svc_destroy_stream(stream);
        want_stream_restart_.store(true);
        NoteStreamStartFailure();
        next_stream_attempt_ =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kStreamRestartBackoffMs);
        return false;
    }

    stream_ = stream;
    stream_active_.store(true);
    want_stream_restart_.store(false);
    consecutive_stream_failures_ = 0;
#if defined(__linux__)
    camera_->ClearRestartRequest();
#endif
    std::cout << "[demo] stream restarted";
#if defined(__linux__)
    std::cout << " camera=" << config_.camera;
#endif
    std::cout << std::endl;
    return true;
}

void PushDemoSession::ServiceTick(std::chrono::steady_clock::time_point now) {
#if defined(__linux__)
    if (camera_->RestartRequested()) {
        want_stream_restart_.store(true);
        camera_->ClearRestartRequest();
    }
#endif

    if (want_stream_restart_.load() && stream_) {
        TeardownStream();
        return;
    }

#if defined(__linux__)
    if (stream_ && !camera_->IsPresent()) {
        TeardownStream();
        return;
    }
#endif

    if (want_connect_restart_.load()) {
        TryReconnectSignaling(now);
    }

#if defined(__linux__)
    if (!want_stream_restart_.load() && stream_) {
        return;
    }
    if (!camera_->IsStableAndReady(now)) {
        return;
    }
#else
    if (!want_stream_restart_.load() && stream_) {
        return;
    }
#endif

    if (now < next_stream_attempt_) {
        return;
    }
    if (!want_stream_restart_.load() && stream_) {
        return;
    }

    if (conn_state_.load() != RFLOW_CONN_CONNECTED) {
        want_connect_restart_.store(true);
        TryReconnectSignaling(now);
        return;
    }

    TryStartStream();
}

void PushDemoSession::StopStreamForExit() {
    if (!stream_) return;
    librflow_svc_stop_stream(stream_);
    librflow_svc_destroy_stream(stream_);
    stream_ = nullptr;
    stream_active_.store(false);
}

}  // namespace rflow::apps::common

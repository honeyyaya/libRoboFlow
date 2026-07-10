#include "media/capture_fps_pipeline_policy.h"

#include <algorithm>
#include <cstdlib>

#include "base/env_reader.h"

namespace rflow::service::impl::policy {

namespace {

constexpr int kHighFpsThreshold = 45;

enum class PipelineMode {
    kAuto,
    kForceLowLatency,
    kForceHighFidelity,
};

PipelineMode ResolvePipelineMode() {
    const char* e = std::getenv("RFLOW_CAPTURE_FPS_PIPELINE");
    if (!e || !e[0]) {
        return PipelineMode::kAuto;
    }
    if (e[0] == 'l' || e[0] == 'L') {
        return PipelineMode::kForceLowLatency;
    }
    if (e[0] == 'h' || e[0] == 'H') {
        return PipelineMode::kForceHighFidelity;
    }
    return PipelineMode::kAuto;
}

CaptureFpsTier TierFromFps(int requested_fps) {
    const int fps = std::max(1, requested_fps);
    return (fps >= kHighFpsThreshold) ? CaptureFpsTier::kHighFidelity : CaptureFpsTier::kLowLatency;
}

void ApplyEnvBackendOverrides(PushStreamerBackendConfig& backend) {
    const char* v4l2_env = std::getenv("RFLOW_V4L2_BUFFER_COUNT");
    if (v4l2_env && v4l2_env[0]) {
        backend.v4l2_buffer_count =
            rflow::common::util::ReadEnvIntInRange("RFLOW_V4L2_BUFFER_COUNT", backend.v4l2_buffer_count, 2, 32);
    }
    const char* qmax_env = std::getenv("RFLOW_MJPEG_QUEUE_MAX");
    if (qmax_env && qmax_env[0]) {
        backend.mjpeg_queue_max =
            rflow::common::util::ReadEnvIntInRange("RFLOW_MJPEG_QUEUE_MAX", backend.mjpeg_queue_max, 1, 32);
    }
    const char* nv12_env = std::getenv("RFLOW_NV12_POOL_SLOTS");
    if (nv12_env && nv12_env[0]) {
        backend.nv12_pool_slots =
            rflow::common::util::ReadEnvIntInRange("RFLOW_NV12_POOL_SLOTS", backend.nv12_pool_slots, 4, 16);
    }
}

void ApplyTierPreset(PushStreamerBackendConfig& backend, CaptureFpsTier tier) {
    switch (tier) {
        case CaptureFpsTier::kHighFidelity:
            backend.mjpeg_queue_latest_only = false;
            // 小幅加深缓冲：+1 V4L2 buf / +1 队列槽，稳 60fps 且 e2e 仅积压时多 ~16ms。
            backend.v4l2_buffer_count       = 5;
            backend.mjpeg_queue_max         = 4;
            backend.nv12_pool_slots         = 7;
            backend.v4l2_poll_timeout_ms    = 5;
            backend.mjpeg_decode_inline     = false;
            break;
        case CaptureFpsTier::kLowLatency:
        default:
            backend.mjpeg_queue_latest_only = true;
            backend.v4l2_buffer_count       = 2;
            backend.mjpeg_queue_max         = 2;
            backend.nv12_pool_slots         = 4;
            backend.v4l2_poll_timeout_ms    = 5;
            backend.mjpeg_decode_inline     = false;
            break;
    }
}

}  // namespace

CaptureFpsTier ClassifyCaptureFpsTier(int requested_fps) {
    switch (ResolvePipelineMode()) {
        case PipelineMode::kForceLowLatency:
            return CaptureFpsTier::kLowLatency;
        case PipelineMode::kForceHighFidelity:
            return CaptureFpsTier::kHighFidelity;
        case PipelineMode::kAuto:
        default:
            return TierFromFps(requested_fps);
    }
}

int MjpegDecodeQueueMaxWaitMsForFps(int requested_fps) {
    const char* env_ms = std::getenv("RFLOW_MJPEG_DECODE_QUEUE_MAX_WAIT_MS");
    if (env_ms && env_ms[0]) {
        return rflow::common::util::ReadEnvIntInRange("RFLOW_MJPEG_DECODE_QUEUE_MAX_WAIT_MS", 25, 0, 5000);
    }
    const CaptureFpsTier tier = ClassifyCaptureFpsTier(requested_fps);
    if (tier == CaptureFpsTier::kHighFidelity) {
        const int fps = std::max(kHighFpsThreshold, requested_fps);
        // ~3 帧周期：60fps 时约 50ms，给解码线程吸收抖动而不长期堆帧。
        const int frame_ms = std::max(1, 1000 / fps);
        return std::min(5000, frame_ms * 3);
    }
    return 25;
}

void ApplyCaptureFpsPipelineDefaults(PushStreamerBackendConfig& backend, int requested_fps) {
    ApplyTierPreset(backend, ClassifyCaptureFpsTier(requested_fps));
    ApplyEnvBackendOverrides(backend);
}

bool ShouldAutoTuneMediaThreadsForFps(int requested_fps) {
    if (requested_fps < kHighFpsThreshold) {
        return false;
    }
    const char* e = std::getenv("RFLOW_MEDIA_THREAD_AUTO");
    if (e && e[0]) {
        const char c = e[0];
        if (c == '0' || c == 'n' || c == 'N' || c == 'f' || c == 'F') {
            return false;
        }
    }
    return true;
}

}  // namespace rflow::service::impl::policy

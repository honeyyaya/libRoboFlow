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

void ApplyTierPreset(PushStreamerBackendConfig& backend, CaptureFpsTier tier) {
    switch (tier) {
        case CaptureFpsTier::kHighFidelity:
            backend.mjpeg_queue_latest_only = false;
            backend.v4l2_buffer_count       = 4;
            backend.mjpeg_queue_max         = 3;
            backend.nv12_pool_slots         = 6;
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
}

}  // namespace rflow::service::impl::policy

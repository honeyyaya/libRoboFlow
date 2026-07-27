#include "rtc/stats/push_pipeline_drop_stats.h"

#include <cstdio>
#include <cstring>

#include "base/logger.h"

namespace rflow::core::rtc {

PushPipelineDropStats& PushPipelineDropStats::Instance() {
    static PushPipelineDropStats inst;
    return inst;
}

void PushPipelineDropStats::OnV4l2FrameCaptured(uint64_t count) {
    if (count == 0) {
        return;
    }
    v4l2_capture_total_.fetch_add(count, std::memory_order_relaxed);
}

void PushPipelineDropStats::OnFrameDispatched(uint64_t count) {
    if (count == 0) {
        return;
    }
    dispatched_total_.fetch_add(count, std::memory_order_relaxed);
}

uint64_t PushPipelineDropStats::v4l2_capture_total() const {
    return v4l2_capture_total_.load(std::memory_order_relaxed);
}

uint64_t PushPipelineDropStats::dispatched_total() const {
    return dispatched_total_.load(std::memory_order_relaxed);
}

void PushPipelineDropStats::OnRgaScaleOk(uint64_t count) {
    if (count == 0) {
        return;
    }
    rga_scale_ok_total_.fetch_add(count, std::memory_order_relaxed);
}

void PushPipelineDropStats::OnRgaScaleFail(uint64_t count) {
    if (count == 0) {
        return;
    }
    rga_scale_fail_total_.fetch_add(count, std::memory_order_relaxed);
}

void PushPipelineDropStats::OnZcFallback(uint64_t count) {
    if (count == 0) {
        return;
    }
    zc_fallback_total_.fetch_add(count, std::memory_order_relaxed);
}

uint64_t PushPipelineDropStats::rga_scale_ok_total() const {
    return rga_scale_ok_total_.load(std::memory_order_relaxed);
}

uint64_t PushPipelineDropStats::rga_scale_fail_total() const {
    return rga_scale_fail_total_.load(std::memory_order_relaxed);
}

uint64_t PushPipelineDropStats::zc_fallback_total() const {
    return zc_fallback_total_.load(std::memory_order_relaxed);
}

void PushPipelineDropStats::LogDrop(const char* module,
                                    uint64_t drop_delta,
                                    uint64_t drop_module_total,
                                    const char* detail) {
    if (!module || drop_delta == 0) {
        return;
    }
    const uint64_t capture = v4l2_capture_total();
    const uint64_t dispatched = dispatched_total();
    char line[640];
    if (detail && detail[0] != '\0') {
        snprintf(line, sizeof(line),
                 "[PushDrop] module=%s | capture_total=%llu | webrtc_in_total=%llu | drop_now=%llu | "
                 "drop_module_total=%llu | %s",
                 module, static_cast<unsigned long long>(capture),
                 static_cast<unsigned long long>(dispatched), static_cast<unsigned long long>(drop_delta),
                 static_cast<unsigned long long>(drop_module_total), detail);
    } else {
        snprintf(line, sizeof(line),
                 "[PushDrop] module=%s | capture_total=%llu | webrtc_in_total=%llu | drop_now=%llu | "
                 "drop_module_total=%llu",
                 module, static_cast<unsigned long long>(capture),
                 static_cast<unsigned long long>(dispatched), static_cast<unsigned long long>(drop_delta),
                 static_cast<unsigned long long>(drop_module_total));
    }
    RFLOW_LOGW("%s", line);
}

}  // namespace rflow::core::rtc

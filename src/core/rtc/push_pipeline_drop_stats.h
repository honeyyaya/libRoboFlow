#ifndef __RFLOW_CORE_RTC_PUSH_PIPELINE_DROP_STATS_H__
#define __RFLOW_CORE_RTC_PUSH_PIPELINE_DROP_STATS_H__

#include <atomic>
#include <cstdint>

namespace rflow::core::rtc {

/// 推流链路丢帧观测：以 V4L2 采集总数为基准，各模块仅在发生丢帧时打印（tag 区分模块）。
class PushPipelineDropStats {
public:
    static PushPipelineDropStats& Instance();

    void OnV4l2FrameCaptured(uint64_t count = 1);
    void OnFrameDispatched(uint64_t count = 1);

    uint64_t v4l2_capture_total() const;
    uint64_t dispatched_total() const;

    void OnRgaScaleOk(uint64_t count = 1);
    void OnRgaScaleFail(uint64_t count = 1);
    void OnZcFallback(uint64_t count = 1);

    uint64_t rga_scale_ok_total() const;
    uint64_t rga_scale_fail_total() const;
    uint64_t zc_fallback_total() const;

    /// drop_delta：本次事件丢帧数；drop_module_total：该模块累计丢帧数。
    void LogDrop(const char* module, uint64_t drop_delta, uint64_t drop_module_total, const char* detail);

private:
    PushPipelineDropStats() = default;

    std::atomic<uint64_t> v4l2_capture_total_{0};
    std::atomic<uint64_t> dispatched_total_{0};
    std::atomic<uint64_t> rga_scale_ok_total_{0};
    std::atomic<uint64_t> rga_scale_fail_total_{0};
    std::atomic<uint64_t> zc_fallback_total_{0};
};

}  // namespace rflow::core::rtc

#endif  // __RFLOW_CORE_RTC_PUSH_PIPELINE_DROP_STATS_H__

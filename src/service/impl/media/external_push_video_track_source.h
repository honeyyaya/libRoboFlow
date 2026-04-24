/**
 * @file   external_push_video_track_source.h
 * @brief  Business-pushed video frames → WebRTC VideoTrackSource
 *
 * CameraVideoTrackSource 只处理 V4L2 采集；当业务侧通过 librflow_svc_push_video_frame
 * 主动投递像素数据时，需要一条独立的源把外部帧接入 WebRTC 编码链路。
 *
 * 支持像素格式：
 *   - I420 (三平面 Y/U/V)
 *   - NV12 (Y + 交错 UV)
 *   - YUYV / UYVY：调用方先转 I420 再 PushI420 (可后续扩展)
 *
 * 线程模型：PushI420 / PushNv12 可在任意业务线程调用，
 * AdaptedVideoTrackSource::OnFrame 本身线程安全，广播到注册的 sinks。
 */

#ifndef RFLOW_SERVICE_IMPL_MEDIA_EXTERNAL_PUSH_VIDEO_TRACK_SOURCE_H_
#define RFLOW_SERVICE_IMPL_MEDIA_EXTERNAL_PUSH_VIDEO_TRACK_SOURCE_H_

#include <atomic>
#include <cstdint>
#include <optional>

#include "media/base/adapted_video_track_source.h"

namespace webrtc_demo {

class ExternalPushVideoTrackSource : public webrtc::AdaptedVideoTrackSource {
public:
    ExternalPushVideoTrackSource();
    ~ExternalPushVideoTrackSource() override;

    ExternalPushVideoTrackSource(const ExternalPushVideoTrackSource&)            = delete;
    ExternalPushVideoTrackSource& operator=(const ExternalPushVideoTrackSource&) = delete;

    /// Push I420 planar frame (contiguous planes assumed via strides).
    /// timestamp_us : 媒体时间戳（微秒）。若为 0 则内部按 steady_clock 生成单调值。
    /// @return true on successfully dispatched to broadcast.
    bool PushI420(const uint8_t* data_y, int stride_y,
                  const uint8_t* data_u, int stride_u,
                  const uint8_t* data_v, int stride_v,
                  int width, int height,
                  int64_t timestamp_us);

    /// Push I420 from a single contiguous buffer with default stride (stride_y=w,
    /// stride_uv=w/2) and standard plane layout Y|U|V. Zero-copy 不保证，SDK 内部会
    /// 按需分配 I420Buffer 并拷贝。
    bool PushI420Contiguous(const uint8_t* buf, uint32_t size,
                            int width, int height,
                            int64_t timestamp_us);

    /// Push NV12 (plane Y + interleaved UV). stride 默认等于 width。
    bool PushNv12(const uint8_t* data_y, int stride_y,
                  const uint8_t* data_uv, int stride_uv,
                  int width, int height,
                  int64_t timestamp_us);

    bool PushNv12Contiguous(const uint8_t* buf, uint32_t size,
                            int width, int height,
                            int64_t timestamp_us);

    /// Frames dispatched counter for debugging/tracing.
    uint32_t pushed_frame_count() const {
        return pushed_.load(std::memory_order_relaxed);
    }

    // AdaptedVideoTrackSource required overrides.
    webrtc::MediaSourceInterface::SourceState state() const override {
        return webrtc::MediaSourceInterface::kLive;
    }
    bool remote() const override { return false; }
    bool is_screencast() const override { return false; }
    std::optional<bool> needs_denoising() const override { return std::nullopt; }

private:
    int64_t ResolveTimestampUs(int64_t provided_us) const;

    std::atomic<uint32_t> pushed_{0};
    mutable std::atomic<int64_t> last_ts_us_{0};
};

}  // namespace webrtc_demo

#endif  // RFLOW_SERVICE_IMPL_MEDIA_EXTERNAL_PUSH_VIDEO_TRACK_SOURCE_H_

#include "media/external_push_video_track_source.h"

#include <chrono>
#include <cstring>

#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_frame.h"
#include "libyuv/convert.h"

namespace webrtc_demo {

namespace {

int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

ExternalPushVideoTrackSource::ExternalPushVideoTrackSource()
    : webrtc::AdaptedVideoTrackSource() {}

ExternalPushVideoTrackSource::~ExternalPushVideoTrackSource() = default;

int64_t ExternalPushVideoTrackSource::ResolveTimestampUs(int64_t provided_us) const {
    if (provided_us > 0) {
        last_ts_us_.store(provided_us, std::memory_order_relaxed);
        return provided_us;
    }
    const int64_t now = NowUs();
    int64_t last = last_ts_us_.load(std::memory_order_relaxed);
    int64_t picked = (last > 0 && now <= last) ? (last + 1) : now;
    last_ts_us_.store(picked, std::memory_order_relaxed);
    return picked;
}

bool ExternalPushVideoTrackSource::PushI420(const uint8_t* data_y, int stride_y,
                                            const uint8_t* data_u, int stride_u,
                                            const uint8_t* data_v, int stride_v,
                                            int width, int height,
                                            int64_t timestamp_us) {
    if (!data_y || !data_u || !data_v || width <= 0 || height <= 0) {
        return false;
    }
    auto buffer = webrtc::I420Buffer::Copy(width, height,
                                           data_y, stride_y,
                                           data_u, stride_u,
                                           data_v, stride_v);
    if (!buffer) {
        return false;
    }

    const int64_t ts_us = ResolveTimestampUs(timestamp_us);
    webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                   .set_video_frame_buffer(buffer)
                                   .set_rotation(webrtc::kVideoRotation_0)
                                   .set_timestamp_us(ts_us)
                                   .build();
    webrtc::AdaptedVideoTrackSource::OnFrame(frame);
    pushed_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool ExternalPushVideoTrackSource::PushI420Contiguous(const uint8_t* buf, uint32_t size,
                                                     int width, int height,
                                                     int64_t timestamp_us) {
    if (!buf || width <= 0 || height <= 0) {
        return false;
    }
    const uint32_t y_size = static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
    const uint32_t uv_w = static_cast<uint32_t>((width + 1) / 2);
    const uint32_t uv_h = static_cast<uint32_t>((height + 1) / 2);
    const uint32_t uv_size = uv_w * uv_h;
    if (size < y_size + 2u * uv_size) {
        return false;
    }
    const uint8_t* y = buf;
    const uint8_t* u = buf + y_size;
    const uint8_t* v = u + uv_size;
    return PushI420(y, width,
                    u, static_cast<int>(uv_w),
                    v, static_cast<int>(uv_w),
                    width, height, timestamp_us);
}

bool ExternalPushVideoTrackSource::PushNv12(const uint8_t* data_y, int stride_y,
                                            const uint8_t* data_uv, int stride_uv,
                                            int width, int height,
                                            int64_t timestamp_us) {
    if (!data_y || !data_uv || width <= 0 || height <= 0) {
        return false;
    }
    // NV12 → I420 拷贝：大多数 WebRTC 编码器（含 Rockchip MPP）对 I420 支持最稳，
    // 并且 AdaptedVideoTrackSource 下游 (broadcaster) 统一以 I420Buffer 发布时
    // 能复用编码器已有的 YUV 路径，避免后续格式侦测分支。
    webrtc::scoped_refptr<webrtc::I420Buffer> buffer =
        webrtc::I420Buffer::Create(width, height);
    if (!buffer) {
        return false;
    }
    const int ret = libyuv::NV12ToI420(data_y, stride_y,
                                        data_uv, stride_uv,
                                        buffer->MutableDataY(), buffer->StrideY(),
                                        buffer->MutableDataU(), buffer->StrideU(),
                                        buffer->MutableDataV(), buffer->StrideV(),
                                        width, height);
    if (ret != 0) {
        return false;
    }

    const int64_t ts_us = ResolveTimestampUs(timestamp_us);
    webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                   .set_video_frame_buffer(buffer)
                                   .set_rotation(webrtc::kVideoRotation_0)
                                   .set_timestamp_us(ts_us)
                                   .build();
    webrtc::AdaptedVideoTrackSource::OnFrame(frame);
    pushed_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool ExternalPushVideoTrackSource::PushNv12Contiguous(const uint8_t* buf, uint32_t size,
                                                     int width, int height,
                                                     int64_t timestamp_us) {
    if (!buf || width <= 0 || height <= 0) {
        return false;
    }
    const uint32_t y_size = static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
    const uint32_t uv_size = static_cast<uint32_t>(width) *
                             static_cast<uint32_t>((height + 1) / 2);
    if (size < y_size + uv_size) {
        return false;
    }
    return PushNv12(buf, width,
                    buf + y_size, width,
                    width, height, timestamp_us);
}

}  // namespace webrtc_demo

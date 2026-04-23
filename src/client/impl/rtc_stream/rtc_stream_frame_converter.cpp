#include "rtc_stream_frame_converter.h"

#include "common/internal/frame_impl.h"
#include "common/internal/logger.h"

#include <chrono>
#include <cstring>
#include <new>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"

namespace rflow::client::impl {

namespace {

uint64_t NowUtcMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

}  // namespace

librflow_video_frame_t MakeVideoFrameFromRtcFrame(const webrtc::VideoFrame& frame,
                                                  int32_t                   stream_index,
                                                  uint32_t                  seq) {
    auto buffer = frame.video_frame_buffer();
    if (!buffer) return nullptr;

    auto i420 = buffer->ToI420();
    if (!i420) {
        RFLOW_LOGW("[frame_conv] ToI420 failed idx=%d", stream_index);
        return nullptr;
    }

    const int w = i420->width();
    const int h = i420->height();
    if (w <= 0 || h <= 0) return nullptr;

    const size_t y_size   = static_cast<size_t>(w) * static_cast<size_t>(h);
    const size_t uv_w     = static_cast<size_t>((w + 1) / 2);
    const size_t uv_h     = static_cast<size_t>((h + 1) / 2);
    const size_t uv_size  = uv_w * uv_h;
    const size_t total    = y_size + 2 * uv_size;

    auto* f = new (std::nothrow) librflow_video_frame_s();
    if (!f) return nullptr;

    f->magic        = rflow::kMagicVideoFrame;
    f->refcount.store(1, std::memory_order_relaxed);
    f->codec        = RFLOW_CODEC_I420;
    f->type         = RFLOW_FRAME_UNKNOWN;
    f->width        = static_cast<uint32_t>(w);
    f->height       = static_cast<uint32_t>(h);
    f->pts_ms       = static_cast<uint64_t>(frame.timestamp_us()) / 1000ULL;
    f->utc_ms       = NowUtcMs();
    f->seq          = seq;
    f->stream_index = stream_index;

    f->payload.resize(total);
    uint8_t* dst = f->payload.data();

    {
        const uint8_t* src = i420->DataY();
        const int      ss  = i420->StrideY();
        for (int row = 0; row < h; ++row) {
            std::memcpy(dst + static_cast<size_t>(row) * static_cast<size_t>(w),
                        src + static_cast<size_t>(row) * static_cast<size_t>(ss),
                        static_cast<size_t>(w));
        }
        dst += y_size;
    }
    {
        const uint8_t* src = i420->DataU();
        const int      ss  = i420->StrideU();
        for (size_t row = 0; row < uv_h; ++row) {
            std::memcpy(dst + row * uv_w, src + row * static_cast<size_t>(ss), uv_w);
        }
        dst += uv_size;
    }
    {
        const uint8_t* src = i420->DataV();
        const int      ss  = i420->StrideV();
        for (size_t row = 0; row < uv_h; ++row) {
            std::memcpy(dst + row * uv_w, src + row * static_cast<size_t>(ss), uv_w);
        }
    }

    return f;
}

}  // namespace rflow::client::impl

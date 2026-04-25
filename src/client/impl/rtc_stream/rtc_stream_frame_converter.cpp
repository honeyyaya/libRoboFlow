#include "rtc_stream_frame_converter.h"

#include "common/internal/frame_impl.h"
#include "common/internal/logger.h"

#include <chrono>
#include <new>

#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
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

void FillCommonFields(librflow_video_frame_s* f,
                      const webrtc::VideoFrame& frame,
                      int32_t stream_index,
                      uint32_t seq,
                      int w,
                      int h,
                      rflow_codec_t codec) {
    f->magic        = rflow::kMagicVideoFrame;
    f->refcount.store(1, std::memory_order_relaxed);
    f->backend      = RFLOW_VIDEO_FRAME_BACKEND_CPU_PLANAR;
    f->native_handle_type = RFLOW_NATIVE_HANDLE_NONE;
    f->codec        = codec;
    f->type         = RFLOW_FRAME_UNKNOWN;
    f->width        = static_cast<uint32_t>(w);
    f->height       = static_cast<uint32_t>(h);
    f->pts_ms       = static_cast<uint64_t>(frame.timestamp_us()) / 1000ULL;
    f->utc_ms       = NowUtcMs();
    f->seq          = seq;
    f->stream_index = stream_index;
}

librflow_video_frame_t MakeVideoFrameFromRtcFrame(const webrtc::VideoFrame& frame,
                                                  int32_t                   stream_index,
                                                  uint32_t                  seq) {
    auto buffer = frame.video_frame_buffer();
    if (!buffer) return nullptr;

    const int w = buffer->width();
    const int h = buffer->height();
    if (w <= 0 || h <= 0) return nullptr;

    auto* f = new (std::nothrow) librflow_video_frame_s();
    if (!f) return nullptr;

    if (const webrtc::NV12BufferInterface* nv12 = buffer->GetNV12()) {
        FillCommonFields(f, frame, stream_index, seq, w, h, RFLOW_CODEC_NV12);
        f->plane_count = 2;
        f->plane_data[0] = nv12->DataY();
        f->plane_data[1] = nv12->DataUV();
        f->plane_strides[0] = static_cast<uint32_t>(nv12->StrideY());
        f->plane_strides[1] = static_cast<uint32_t>(nv12->StrideUV());
        f->plane_widths[0] = static_cast<uint32_t>(w);
        f->plane_heights[0] = static_cast<uint32_t>(h);
        f->plane_widths[1] = static_cast<uint32_t>(nv12->ChromaWidth() * 2);
        f->plane_heights[1] = static_cast<uint32_t>(nv12->ChromaHeight());
        f->buffer_ref = buffer;
        return f;
    }

    const webrtc::I420BufferInterface* i420 = buffer->GetI420();
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> backing = buffer;
    webrtc::scoped_refptr<webrtc::I420BufferInterface> converted_i420;
    if (!i420) {
        converted_i420 = buffer->ToI420();
        if (!converted_i420) {
            RFLOW_LOGW("[frame_conv] ToI420 failed idx=%d", stream_index);
            delete f;
            return nullptr;
        }
        i420 = converted_i420.get();
        backing = converted_i420;
    }

    FillCommonFields(f, frame, stream_index, seq, w, h, RFLOW_CODEC_I420);
    f->plane_count = 3;
    f->plane_data[0] = i420->DataY();
    f->plane_data[1] = i420->DataU();
    f->plane_data[2] = i420->DataV();
    f->plane_strides[0] = static_cast<uint32_t>(i420->StrideY());
    f->plane_strides[1] = static_cast<uint32_t>(i420->StrideU());
    f->plane_strides[2] = static_cast<uint32_t>(i420->StrideV());
    f->plane_widths[0] = static_cast<uint32_t>(w);
    f->plane_heights[0] = static_cast<uint32_t>(h);
    f->plane_widths[1] = static_cast<uint32_t>((w + 1) / 2);
    f->plane_heights[1] = static_cast<uint32_t>((h + 1) / 2);
    f->plane_widths[2] = f->plane_widths[1];
    f->plane_heights[2] = f->plane_heights[1];
    f->buffer_ref = backing;

    return f;
}

}  // namespace rflow::client::impl

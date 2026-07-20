#include "platform/rockchip/native_dec_frame_buffer.h"

#include "platform/rockchip/mjpeg_decoder.h"
#include "platform/rockchip/rga_nv12_scale.h"
#include "public/log_tagged.h"
#include "rtc/push_pipeline_drop_stats.h"

#include <cstring>

#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "rtc_base/ref_counted_object.h"
#include "third_party/libyuv/include/libyuv/convert.h"

#include "mpp_buffer.h"
#include "mpp_frame.h"
#include "rk_mpi.h"
#include "rk_type.h"

namespace rflow::rtc::hw::rockchip_mpp {

namespace {

bool CopyMppSemiPlanarToNv12(RK_U32 fmt,
                             const uint8_t* src_y,
                             const uint8_t* src_uv,
                             int src_stride,
                             int width,
                             int height,
                             webrtc::NV12Buffer* out) {
    if (!out || width <= 0 || height <= 0) {
        return false;
    }
    uint8_t* dst_y = out->MutableDataY();
    uint8_t* dst_uv = out->MutableDataUV();
    const int dst_sy = out->StrideY();
    const int dst_suv = out->StrideUV();
    if (fmt == MPP_FMT_YUV420SP) {
        for (int r = 0; r < height; ++r) {
            std::memcpy(dst_y + r * dst_sy, src_y + r * src_stride, static_cast<size_t>(width));
        }
        for (int r = 0; r < height / 2; ++r) {
            std::memcpy(dst_uv + r * dst_suv, src_uv + r * src_stride, static_cast<size_t>(width));
        }
        return true;
    }
    if (fmt == MPP_FMT_YUV420SP_VU) {
        return libyuv::NV21ToNV12(src_y, src_stride, src_uv, src_stride, dst_y, dst_sy, dst_uv, dst_suv, width,
                                  height) == 0;
    }
    return false;
}

const uint8_t* MppBufferBasePtr(MppBuffer buf) {
    return buf ? static_cast<const uint8_t*>(mpp_buffer_get_ptr(buf)) : nullptr;
}

}  // namespace

// static
webrtc::scoped_refptr<MppNativeDecFrameBuffer> MppNativeDecFrameBuffer::CreateFromMppFrame(
    void* mpp_frame,
    int width,
    int height,
    int hor_stride,
    int ver_stride,
    uint32_t mpp_fmt,
    int64_t mjpeg_input_timestamp_us,
    int64_t dq_time_us,
    int64_t v4l2_timestamp_us,
    int64_t poll_wait_us,
    int64_t dqbuf_ioctl_us,
    int64_t decode_queue_wait_us,
    int64_t wall_capture_utc_ms,
    std::shared_ptr<RkMppMjpegDecoder> decoder_keepalive) {
    if (!mpp_frame) {
        return nullptr;
    }
    return webrtc::scoped_refptr<MppNativeDecFrameBuffer>(
        new webrtc::RefCountedObject<MppNativeDecFrameBuffer>(mpp_frame, width, height, hor_stride, ver_stride,
                                                              mpp_fmt, mjpeg_input_timestamp_us, dq_time_us,
                                                              v4l2_timestamp_us, poll_wait_us, dqbuf_ioctl_us,
                                                              decode_queue_wait_us, wall_capture_utc_ms,
                                                              std::move(decoder_keepalive)));
}

// static
webrtc::scoped_refptr<MppNativeDecFrameBuffer> MppNativeDecFrameBuffer::CreateFromOwnedScaleBuffer(
    void* mpp_buffer,
    int width,
    int height,
    int hor_stride,
    int ver_stride,
    uint32_t mpp_fmt,
    std::shared_ptr<RkMppMjpegDecoder> decoder_keepalive) {
    if (!mpp_buffer) {
        return nullptr;
    }
    return webrtc::scoped_refptr<MppNativeDecFrameBuffer>(new webrtc::RefCountedObject<MppNativeDecFrameBuffer>(
        mpp_buffer, width, height, hor_stride, ver_stride, mpp_fmt, std::move(decoder_keepalive), true));
}

MppNativeDecFrameBuffer* MppNativeDecFrameBuffer::TryGet(const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& buffer) {
    if (!buffer || buffer->type() != webrtc::VideoFrameBuffer::Type::kNative) {
        return nullptr;
    }
    return dynamic_cast<MppNativeDecFrameBuffer*>(buffer.get());
}

MppNativeDecFrameBuffer::MppNativeDecFrameBuffer(void* mpp_frame,
                                                 int width,
                                                 int height,
                                                 int hor_stride,
                                                 int ver_stride,
                                                 uint32_t mpp_fmt,
                                                 int64_t mjpeg_input_timestamp_us,
                                                 int64_t dq_time_us,
                                                 int64_t v4l2_timestamp_us,
                                                 int64_t poll_wait_us,
                                                 int64_t dqbuf_ioctl_us,
                                                 int64_t decode_queue_wait_us,
                                                 int64_t wall_capture_utc_ms,
                                                 std::shared_ptr<RkMppMjpegDecoder> decoder_keepalive)
    : frame_(mpp_frame),
      width_(width),
      height_(height),
      hor_stride_(hor_stride),
      ver_stride_(ver_stride),
      mpp_fmt_(mpp_fmt),
      mjpeg_input_timestamp_us_(mjpeg_input_timestamp_us),
      dq_time_us_(dq_time_us),
      v4l2_timestamp_us_(v4l2_timestamp_us),
      poll_wait_us_(poll_wait_us),
      dqbuf_ioctl_us_(dqbuf_ioctl_us),
      decode_queue_wait_us_(decode_queue_wait_us),
      wall_capture_utc_ms_(wall_capture_utc_ms),
      decoder_keepalive_(std::move(decoder_keepalive)) {}

MppNativeDecFrameBuffer::MppNativeDecFrameBuffer(void* mpp_buffer,
                                                 int width,
                                                 int height,
                                                 int hor_stride,
                                                 int ver_stride,
                                                 uint32_t mpp_fmt,
                                                 std::shared_ptr<RkMppMjpegDecoder> decoder_keepalive,
                                                 bool from_scale_pool)
    : owned_mpp_buf_(mpp_buffer),
      from_scale_pool_(from_scale_pool),
      width_(width),
      height_(height),
      hor_stride_(hor_stride),
      ver_stride_(ver_stride),
      mpp_fmt_(mpp_fmt),
      decoder_keepalive_(std::move(decoder_keepalive)) {}

void MppNativeDecFrameBuffer::ReleaseOwnedBuffer() {
    if (!owned_mpp_buf_) {
        return;
    }
    if (from_scale_pool_) {
        MppDrmScaleBufferPool::Instance().Release(reinterpret_cast<MppBuffer>(owned_mpp_buf_));
    } else {
        mpp_buffer_put(reinterpret_cast<MppBuffer>(owned_mpp_buf_));
    }
    owned_mpp_buf_ = nullptr;
}

MppNativeDecFrameBuffer::~MppNativeDecFrameBuffer() {
    if (frame_) {
        MppFrame f = static_cast<MppFrame>(frame_);
        mpp_frame_deinit(&f);
        frame_ = nullptr;
    }
    ReleaseOwnedBuffer();
}

webrtc::VideoFrameBuffer::Type MppNativeDecFrameBuffer::type() const {
    return Type::kNative;
}

int MppNativeDecFrameBuffer::width() const {
    return width_;
}

int MppNativeDecFrameBuffer::height() const {
    return height_;
}

void* MppNativeDecFrameBuffer::mpp_buffer_handle() const {
    if (owned_mpp_buf_) {
        return owned_mpp_buf_;
    }
    if (!frame_) {
        return nullptr;
    }
    MppFrame f = static_cast<MppFrame>(frame_);
    return mpp_frame_get_buffer(f);
}

webrtc::scoped_refptr<webrtc::I420BufferInterface> MppNativeDecFrameBuffer::ToI420() {
    MppBuffer mbuf = reinterpret_cast<MppBuffer>(mpp_buffer_handle());
    if (!mbuf) {
        return nullptr;
    }
    const auto* yuv = MppBufferBasePtr(mbuf);
    if (!yuv) {
        return nullptr;
    }
    const int hs = hor_stride_;
    const int ver_stride = ver_stride_;
    const uint8_t* src_y = yuv;
    const uint8_t* src_uv = yuv + static_cast<size_t>(hs) * static_cast<size_t>(ver_stride);
    webrtc::scoped_refptr<webrtc::I420Buffer> i420 = webrtc::I420Buffer::Create(width_, height_);
    const RK_U32 fmt = static_cast<RK_U32>(mpp_fmt_);
    int conv = -1;
    if (fmt == MPP_FMT_YUV420SP) {
        conv = libyuv::NV12ToI420(src_y, hs, src_uv, hs, i420->MutableDataY(), i420->StrideY(), i420->MutableDataU(),
                                  i420->StrideU(), i420->MutableDataV(), i420->StrideV(), width_, height_);
    } else if (fmt == MPP_FMT_YUV420SP_VU) {
        conv = libyuv::NV21ToI420(src_y, hs, src_uv, hs, i420->MutableDataY(), i420->StrideY(), i420->MutableDataU(),
                                  i420->StrideU(), i420->MutableDataV(), i420->StrideV(), width_, height_);
    }
    if (conv != 0) {
        return nullptr;
    }
    return i420;
}

webrtc::scoped_refptr<webrtc::VideoFrameBuffer> MppNativeDecFrameBuffer::GetMappedFrameBuffer(
    webrtc::ArrayView<Type> types) {
    for (webrtc::VideoFrameBuffer::Type t : types) {
        if (t == Type::kNV12) {
            webrtc::scoped_refptr<webrtc::NV12Buffer> nv12 = webrtc::NV12Buffer::Create(width_, height_);
            MppBuffer mbuf = reinterpret_cast<MppBuffer>(mpp_buffer_handle());
            if (!nv12 || !mbuf) {
                return nullptr;
            }
            const auto* yuv = MppBufferBasePtr(mbuf);
            if (!yuv) {
                return nullptr;
            }
            const uint8_t* src_y = yuv;
            const uint8_t* src_uv = yuv + static_cast<size_t>(hor_stride_) * static_cast<size_t>(ver_stride_);
            if (!CopyMppSemiPlanarToNv12(static_cast<RK_U32>(mpp_fmt_), src_y, src_uv, hor_stride_, width_, height_,
                                         nv12.get())) {
                return nullptr;
            }
            return nv12;
        }
    }
    return nullptr;
}

webrtc::scoped_refptr<webrtc::VideoFrameBuffer> MppNativeDecFrameBuffer::CropAndScale(int offset_x,
                                                                                      int offset_y,
                                                                                      int crop_width,
                                                                                      int crop_height,
                                                                                      int scaled_width,
                                                                                      int scaled_height) {
    if (crop_width <= 0 || crop_height <= 0 || scaled_width <= 0 || scaled_height <= 0) {
        return nullptr;
    }
    if (offset_x == 0 && offset_y == 0 && crop_width == width_ && crop_height == height_ && scaled_width == width_ &&
        scaled_height == height_) {
        return webrtc::scoped_refptr<MppNativeDecFrameBuffer>(
            static_cast<MppNativeDecFrameBuffer*>(const_cast<MppNativeDecFrameBuffer*>(this)));
    }

    const RK_U32 fmt = static_cast<RK_U32>(mpp_fmt_);
    if (fmt != MPP_FMT_YUV420SP && fmt != MPP_FMT_YUV420SP_VU) {
        webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 = ToI420();
        return i420 ? i420->CropAndScale(offset_x, offset_y, crop_width, crop_height, scaled_width, scaled_height)
                    : nullptr;
    }

    MppBuffer src_buf = reinterpret_cast<MppBuffer>(mpp_buffer_handle());
    if (!src_buf) {
        return nullptr;
    }
    const int src_fd = mpp_buffer_get_fd(src_buf);
    const size_t src_size = static_cast<size_t>(mpp_buffer_get_size(src_buf));

#if defined(RFLOW_HAVE_LIBRGA)
    int dst_hs = 0;
    int dst_vs = 0;
    size_t dst_size = 0;
    MppBuffer dst_buf =
        MppDrmScaleBufferPool::Instance().Acquire(scaled_width, scaled_height, &dst_hs, &dst_vs, &dst_size);
    if (dst_buf) {
        const int dst_fd = mpp_buffer_get_fd(dst_buf);
        RgaNv12CropScaleParams params{};
        params.src_fd = src_fd;
        params.src_buf_size = src_size;
        params.src_width = width_;
        params.src_height = height_;
        params.src_hor_stride = hor_stride_;
        params.src_ver_stride = ver_stride_;
        params.src_mpp_fmt = fmt;
        params.offset_x = offset_x;
        params.offset_y = offset_y;
        params.crop_width = crop_width;
        params.crop_height = crop_height;
        params.scaled_width = scaled_width;
        params.scaled_height = scaled_height;
        params.dst_fd = dst_fd;
        params.dst_buf_size = dst_size;
        params.dst_hor_stride = dst_hs;
        params.dst_ver_stride = dst_vs;
        if (RgaNv12CropScaleDmabuf(params)) {
            rflow::core::rtc::PushPipelineDropStats::Instance().OnRgaScaleOk(1);
            return CreateFromOwnedScaleBuffer(dst_buf, scaled_width, scaled_height, dst_hs, dst_vs, MPP_FMT_YUV420SP,
                                              decoder_keepalive_);
        }
        rflow::core::rtc::PushPipelineDropStats::Instance().OnRgaScaleFail(1);
        MppDrmScaleBufferPool::Instance().Release(dst_buf);
    } else {
        rflow::core::rtc::PushPipelineDropStats::Instance().OnRgaScaleFail(1);
    }
#else
    (void)src_buf;
    (void)src_fd;
    (void)src_size;
    (void)fmt;
#endif

    webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 = ToI420();
    return i420 ? i420->CropAndScale(offset_x, offset_y, crop_width, crop_height, scaled_width, scaled_height)
                  : nullptr;
}

std::string MppNativeDecFrameBuffer::storage_representation() const {
    return from_scale_pool_ ? "mpp_rga_scale_drm_frame" : "mpp_mjpeg_dec_drm_frame";
}

}  // namespace rflow::rtc::hw::rockchip_mpp

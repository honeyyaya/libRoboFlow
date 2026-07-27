#include "hw/frame/webrtc_frame_bridge.h"

#include "hw/frame/simple_video_frame.h"
#include "platform/rockchip/mpp/mpp_hw_frame_buffer.h"
#include "platform/rockchip/native_dec_frame_buffer.h"

#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_frame_type.h"
#include "api/video/nv12_buffer.h"

#include <cstring>

#include "third_party/libyuv/include/libyuv/convert.h"

namespace rflow::hw {
namespace {

class CpuHostFrameBuffer : public IFrameBuffer {
 public:
  CpuHostFrameBuffer(HwPixelFormat fmt,
                     int width,
                     int height,
                     std::vector<const uint8_t*> planes,
                     std::vector<uint32_t> strides,
                     HwBackendId backend = HwBackendId::kBuiltin)
      : fmt_(fmt),
        width_(width),
        height_(height),
        planes_(std::move(planes)),
        strides_(std::move(strides)),
        backend_(backend) {}

  HwMemoryType memory_type() const override { return HwMemoryType::kCpuHost; }
  HwPixelFormat pixel_format() const override { return fmt_; }
  HwBackendId backend_id() const override { return backend_; }
  int width() const override { return width_; }
  int height() const override { return height_; }
  uint32_t plane_count() const override { return static_cast<uint32_t>(planes_.size()); }

  PlaneView plane(uint32_t index) const override {
    PlaneView view;
    if (index >= planes_.size()) {
      return view;
    }
    view.data = planes_[index];
    view.stride = index < strides_.size() ? strides_[index] : 0;
    view.width = static_cast<uint32_t>(width_);
    view.height = (index == 0) ? static_cast<uint32_t>(height_) : static_cast<uint32_t>(height_ / 2);
    return view;
  }

 private:
  HwPixelFormat fmt_;
  int width_;
  int height_;
  std::vector<const uint8_t*> planes_;
  std::vector<uint32_t> strides_;
  HwBackendId backend_;
};

std::shared_ptr<IFrameBuffer> FrameBufferFromWebrtc(const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& vfb) {
  if (!vfb) {
    return nullptr;
  }
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
  if (auto* native = TryGetMppNativeFromWebrtcBuffer(vfb)) {
    webrtc::scoped_refptr<rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer> mpp_ref(
        static_cast<rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer*>(native));
    return rockchip::WrapMppNativeBuffer(mpp_ref);
  }
#endif
  if (vfb->type() == webrtc::VideoFrameBuffer::Type::kI420) {
    const webrtc::I420BufferInterface* i420 = vfb->GetI420();
    if (!i420) {
      return nullptr;
    }
    return std::make_shared<CpuHostFrameBuffer>(
        HwPixelFormat::kI420, i420->width(), i420->height(),
        std::vector<const uint8_t*>{i420->DataY(), i420->DataU(), i420->DataV()},
        std::vector<uint32_t>{static_cast<uint32_t>(i420->StrideY()), static_cast<uint32_t>(i420->StrideU()),
                              static_cast<uint32_t>(i420->StrideV())});
  }
  return nullptr;
}

}  // namespace

rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer* TryGetMppNativeFromWebrtcBuffer(
    const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& buffer) {
  if (!buffer) {
    return nullptr;
  }
  if (auto* native = rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer::TryGet(buffer)) {
    return native;
  }
  return nullptr;
}

webrtc::VideoFrame ToWebrtcVideoFrame(const IVideoFrame& frame) {
  const FrameMetadata meta = frame.metadata();
  webrtc::scoped_refptr<webrtc::VideoFrameBuffer> vfb;
  if (const auto buffer = frame.buffer()) {
    if (auto native = rockchip::UnwrapMppNativeBuffer(buffer)) {
      vfb = native;
    } else if (buffer->pixel_format() == HwPixelFormat::kNv12 && buffer->plane_count() >= 2) {
      const PlaneView y = buffer->plane(0);
      const PlaneView uv = buffer->plane(1);
      if (y.data && uv.data) {
        auto nv12 = webrtc::NV12Buffer::Create(buffer->width(), buffer->height());
        for (int row = 0; row < buffer->height(); ++row) {
          std::memcpy(nv12->MutableDataY() + row * nv12->StrideY(), y.data + row * y.stride,
                      static_cast<size_t>(buffer->width()));
        }
        for (int row = 0; row < buffer->height() / 2; ++row) {
          std::memcpy(nv12->MutableDataUV() + row * nv12->StrideUV(), uv.data + row * uv.stride,
                      static_cast<size_t>(buffer->width()));
        }
        vfb = nv12;
      }
    } else if (buffer->pixel_format() == HwPixelFormat::kI420 && buffer->plane_count() >= 3) {
      const PlaneView y = buffer->plane(0);
      const PlaneView u = buffer->plane(1);
      const PlaneView v = buffer->plane(2);
      if (y.data && u.data && v.data) {
        auto i420 = webrtc::I420Buffer::Create(buffer->width(), buffer->height());
        libyuv::I420Copy(y.data, y.stride, u.data, u.stride, v.data, v.stride, i420->MutableDataY(),
                         i420->StrideY(), i420->MutableDataU(), i420->StrideU(), i420->MutableDataV(),
                         i420->StrideV(), buffer->width(), buffer->height());
        vfb = i420;
      }
    }
  }
  return webrtc::VideoFrame::Builder()
      .set_video_frame_buffer(vfb)
      .set_timestamp_us(meta.pts_us)
      .build();
}

std::shared_ptr<IVideoFrame> FromWebrtcVideoFrame(const webrtc::VideoFrame& frame) {
  auto buffer = FrameBufferFromWebrtc(frame.video_frame_buffer());
  if (!buffer) {
    return nullptr;
  }
  FrameMetadata meta;
  meta.pts_us = frame.timestamp_us();
  meta.codec = buffer->pixel_format() == HwPixelFormat::kI420 ? HwCodecId::kI420 : HwCodecId::kNv12;
  return std::make_shared<SimpleVideoFrame>(std::move(buffer), meta);
}

std::shared_ptr<IEncodedPacket> EncodedPacketFromWebrtcImage(const webrtc::EncodedImage& image,
                                                              HwCodecId codec) {
  if (!image.data() || image.size() == 0) {
    return nullptr;
  }
  std::vector<uint8_t> bytes(image.data(), image.data() + image.size());
  return std::make_shared<EncodedPacketBuffer>(
      std::move(bytes), codec, image.FrameType() == webrtc::VideoFrameType::kVideoFrameKey, 0, true);
}

void StampNativeFrameEnterTime(const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& buffer,
                               int64_t t_us) {
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
  if (auto* native = TryGetMppNativeFromWebrtcBuffer(buffer)) {
    native->SetOnFrameEnterUs(t_us);
  }
#else
  (void)buffer;
  (void)t_us;
#endif
}

}  // namespace rflow::hw

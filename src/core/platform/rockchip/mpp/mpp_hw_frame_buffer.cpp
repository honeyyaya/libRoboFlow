#include "platform/rockchip/mpp/mpp_hw_frame_buffer.h"

#include "platform/rockchip/native_dec_frame_buffer.h"

#include "mpp_buffer.h"
#include "mpp_frame.h"
#include "rk_type.h"

namespace rflow::hw::rockchip {
namespace {

HwPixelFormat MppFormatToHwPixel(uint32_t mpp_fmt) {
  switch (static_cast<RK_U32>(mpp_fmt)) {
    case MPP_FMT_YUV420SP:
    case MPP_FMT_YUV420SP_VU:
      return HwPixelFormat::kNv12;
    default:
      return HwPixelFormat::kNativeVendor;
  }
}

}  // namespace

MppHwFrameBuffer::MppHwFrameBuffer(
    webrtc::scoped_refptr<rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer> native)
    : native_(std::move(native)) {}

HwMemoryType MppHwFrameBuffer::memory_type() const { return HwMemoryType::kDmabufFd; }

HwPixelFormat MppHwFrameBuffer::pixel_format() const {
  return native_ ? MppFormatToHwPixel(native_->mpp_fmt()) : HwPixelFormat::kUnknown;
}

HwBackendId MppHwFrameBuffer::backend_id() const { return HwBackendId::kRockchipMpp; }

int MppHwFrameBuffer::width() const { return native_ ? native_->width() : 0; }

int MppHwFrameBuffer::height() const { return native_ ? native_->height() : 0; }

uint32_t MppHwFrameBuffer::plane_count() const { return 2; }

PlaneView MppHwFrameBuffer::plane(uint32_t index) const {
  PlaneView view;
  if (!native_) {
    return view;
  }
  void* handle = native_->mpp_buffer_handle();
  if (!handle) {
    return view;
  }
  MppBuffer buf = reinterpret_cast<MppBuffer>(handle);
  const auto* base = static_cast<const uint8_t*>(mpp_buffer_get_ptr(buf));
  if (!base) {
    return view;
  }
  const int hs = native_->hor_stride();
  const int vs = native_->ver_stride();
  if (index == 0) {
    view.data = base;
    view.stride = static_cast<uint32_t>(hs);
    view.width = static_cast<uint32_t>(width());
    view.height = static_cast<uint32_t>(height());
  } else if (index == 1) {
    view.data = base + static_cast<size_t>(hs) * static_cast<size_t>(vs);
    view.stride = static_cast<uint32_t>(hs);
    view.width = static_cast<uint32_t>(width());
    view.height = static_cast<uint32_t>(height() / 2);
  }
  return view;
}

int MppHwFrameBuffer::dmabuf_fd(int /*plane*/) const {
  if (!native_) {
    return -1;
  }
  void* handle = native_->mpp_buffer_handle();
  if (!handle) {
    return -1;
  }
  return mpp_buffer_get_fd(reinterpret_cast<MppBuffer>(handle));
}

void* MppHwFrameBuffer::native_handle(HwNativeHandleTag* out_tag) const {
  if (out_tag) {
    *out_tag = native_ && native_->mpp_frame() ? HwNativeHandleTag::kRockchipMppFrame
                                               : HwNativeHandleTag::kRockchipMppBuffer;
  }
  if (!native_) {
    return nullptr;
  }
  if (native_->mpp_frame()) {
    return native_->mpp_frame();
  }
  return native_->mpp_buffer_handle();
}

std::shared_ptr<IFrameBuffer> MppHwFrameBuffer::MapToCpu(HwPixelFormat /*want*/) const {
  return nullptr;
}

rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer* MppHwFrameBuffer::native() const {
  return native_.get();
}

webrtc::scoped_refptr<rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer> MppHwFrameBuffer::native_ref() const {
  return native_;
}

std::shared_ptr<IFrameBuffer> WrapMppNativeBuffer(
    webrtc::scoped_refptr<rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer> native) {
  if (!native) {
    return nullptr;
  }
  return std::make_shared<MppHwFrameBuffer>(std::move(native));
}

MppHwFrameBuffer* TryGetMppHwFrameBuffer(const std::shared_ptr<IFrameBuffer>& buffer) {
  return dynamic_cast<MppHwFrameBuffer*>(buffer.get());
}

webrtc::scoped_refptr<rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer> UnwrapMppNativeBuffer(
    const std::shared_ptr<IFrameBuffer>& buffer) {
  if (auto* mpp = TryGetMppHwFrameBuffer(buffer)) {
    return mpp->native_ref();
  }
  return nullptr;
}

}  // namespace rflow::hw::rockchip

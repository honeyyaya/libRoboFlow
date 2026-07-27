#ifndef __RFLOW_CORE_PLATFORM_ROCKCHIP_MPP_HW_FRAME_BUFFER_H__
#define __RFLOW_CORE_PLATFORM_ROCKCHIP_MPP_HW_FRAME_BUFFER_H__

#include "hw/frame/frame_buffer.h"

#include "api/scoped_refptr.h"
#include "api/video/video_frame_buffer.h"

namespace rflow::rtc::hw::rockchip_mpp {
class MppNativeDecFrameBuffer;
}

namespace rflow::hw::rockchip {

/// IFrameBuffer 适配：包装现有 MppNativeDecFrameBuffer，供 core/hw 抽象层消费。
class MppHwFrameBuffer : public IFrameBuffer {
 public:
  explicit MppHwFrameBuffer(webrtc::scoped_refptr<rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer> native);

  HwMemoryType memory_type() const override;
  HwPixelFormat pixel_format() const override;
  HwBackendId backend_id() const override;
  int width() const override;
  int height() const override;
  uint32_t plane_count() const override;
  PlaneView plane(uint32_t index) const override;
  int dmabuf_fd(int plane = 0) const override;
  void* native_handle(HwNativeHandleTag* out_tag) const override;
  std::shared_ptr<IFrameBuffer> MapToCpu(HwPixelFormat want) const override;

  rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer* native() const;
  webrtc::scoped_refptr<rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer> native_ref() const;

 private:
  webrtc::scoped_refptr<rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer> native_;
};

std::shared_ptr<IFrameBuffer> WrapMppNativeBuffer(
    webrtc::scoped_refptr<rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer> native);

MppHwFrameBuffer* TryGetMppHwFrameBuffer(const std::shared_ptr<IFrameBuffer>& buffer);

webrtc::scoped_refptr<rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer> UnwrapMppNativeBuffer(
    const std::shared_ptr<IFrameBuffer>& buffer);

}  // namespace rflow::hw::rockchip

#endif  // __RFLOW_CORE_PLATFORM_ROCKCHIP_MPP_HW_FRAME_BUFFER_H__

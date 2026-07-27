#include "hw/capture/mjpeg_hw_capture.h"

#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
#include "platform/rockchip/capture/rockchip_mjpeg_hw_capture.h"
#endif

namespace rflow::hw::capture {

std::shared_ptr<IMjpegHwCapture> CreateMjpegHwCapture() {
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
  return rockchip::CreateRockchipMjpegHwCapture();
#else
  return nullptr;
#endif
}

}  // namespace rflow::hw::capture

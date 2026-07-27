#ifndef __RFLOW_CORE_PLATFORM_ROCKCHIP_CAPTURE_ROCKCHIP_MJPEG_HW_CAPTURE_H__
#define __RFLOW_CORE_PLATFORM_ROCKCHIP_CAPTURE_ROCKCHIP_MJPEG_HW_CAPTURE_H__

#include <memory>

#include "hw/capture/mjpeg_hw_capture.h"

namespace rflow::hw::capture::rockchip {

std::shared_ptr<IMjpegHwCapture> CreateRockchipMjpegHwCapture();

}  // namespace rflow::hw::capture::rockchip

#endif  // __RFLOW_CORE_PLATFORM_ROCKCHIP_CAPTURE_ROCKCHIP_MJPEG_HW_CAPTURE_H__

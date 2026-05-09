#ifndef RFLOW_HW_ROCKCHIP_MPP_VIDEO_ENCODER_FACTORY_H_
#define RFLOW_HW_ROCKCHIP_MPP_VIDEO_ENCODER_FACTORY_H_

#include <memory>

namespace webrtc {
class VideoEncoderFactory;
}

namespace rflow::rtc::hw::rockchip_mpp {

/// H.264 优先走 Rockchip MPP 硬件编码，失败时由 libwebrtc 内置 OpenH264 回退。
std::unique_ptr<webrtc::VideoEncoderFactory> CreateVideoEncoderFactory();

}  // namespace rflow::rtc::hw::rockchip_mpp

#endif  // RFLOW_HW_ROCKCHIP_MPP_VIDEO_ENCODER_FACTORY_H_


#ifndef RFLOW_HW_ROCKCHIP_MPP_VIDEO_ENCODER_FACTORY_H_
#define RFLOW_HW_ROCKCHIP_MPP_VIDEO_ENCODER_FACTORY_H_

#include <memory>

namespace webrtc {
class VideoEncoderFactory;
}

namespace rflow::rtc::hw::rockchip_mpp {

/// H.264 优先走 Rockchip MPP 硬件编码，失败时由 libwebrtc 内置 OpenH264 回退。
/// @param mpp_rc_cbr true 时 MPP rc:mode=CBR，否则 VBR。
std::unique_ptr<webrtc::VideoEncoderFactory> CreateVideoEncoderFactory(bool mpp_rc_cbr = false);

}  // namespace rflow::rtc::hw::rockchip_mpp

#endif  // RFLOW_HW_ROCKCHIP_MPP_VIDEO_ENCODER_FACTORY_H_


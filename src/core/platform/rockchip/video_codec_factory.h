#ifndef RFLOW_HW_ROCKCHIP_MPP_VIDEO_CODEC_FACTORY_H_
#define RFLOW_HW_ROCKCHIP_MPP_VIDEO_CODEC_FACTORY_H_

#include <memory>

namespace webrtc {
class VideoDecoderFactory;
class VideoEncoderFactory;
}

namespace rflow::rtc::hw::rockchip_mpp {

/// H.264 优先走 Rockchip MPP 硬件编码，失败时由 libwebrtc 内置 OpenH264 回退。
/// @param mpp_rc_cbr true 时 MPP rc:mode=CBR，否则 VBR。
std::unique_ptr<webrtc::VideoEncoderFactory> CreateVideoEncoderFactory(bool mpp_rc_cbr = false);

/// H.264 优先走 Rockchip MPP 硬件解码；其它格式仍用 libwebrtc 内置解码器。
std::unique_ptr<webrtc::VideoDecoderFactory> CreateVideoDecoderFactory();

}  // namespace rflow::rtc::hw::rockchip_mpp

#endif  // RFLOW_HW_ROCKCHIP_MPP_VIDEO_CODEC_FACTORY_H_

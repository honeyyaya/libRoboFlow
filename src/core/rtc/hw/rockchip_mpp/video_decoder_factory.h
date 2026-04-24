#ifndef WEBRTC_DEMO_HW_ROCKCHIP_MPP_VIDEO_DECODER_FACTORY_H_
#define WEBRTC_DEMO_HW_ROCKCHIP_MPP_VIDEO_DECODER_FACTORY_H_

#include <memory>

namespace webrtc {
class VideoDecoderFactory;
}

namespace rflow::rtc::hw::rockchip_mpp {

/// H.264 优先走 Rockchip MPP 硬件解码；其它格式仍用 libwebrtc 内置解码器。
std::unique_ptr<webrtc::VideoDecoderFactory> CreateVideoDecoderFactory();

}  // namespace rflow::rtc::hw::rockchip_mpp

#endif


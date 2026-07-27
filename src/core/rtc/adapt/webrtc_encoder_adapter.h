#ifndef __RFLOW_CORE_RTC_ADAPT_WEBRTC_ENCODER_ADAPTER_H__
#define __RFLOW_CORE_RTC_ADAPT_WEBRTC_ENCODER_ADAPTER_H__

#include <memory>

#include "hw/codec/encoder.h"

namespace webrtc {
class VideoEncoder;
}

namespace rflow::rtc::adapt {

/// 将 rflow::hw::IVideoEncoder 桥接为 webrtc::VideoEncoder（供 WebRTC 工厂按需选用）。
std::unique_ptr<webrtc::VideoEncoder> CreateWebrtcEncoderAdapter(
    std::unique_ptr<rflow::hw::IVideoEncoder> encoder);

}  // namespace rflow::rtc::adapt

#endif  // __RFLOW_CORE_RTC_ADAPT_WEBRTC_ENCODER_ADAPTER_H__

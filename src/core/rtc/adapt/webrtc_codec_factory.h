#ifndef __RFLOW_CORE_RTC_ADAPT_WEBRTC_CODEC_FACTORY_H__
#define __RFLOW_CORE_RTC_ADAPT_WEBRTC_CODEC_FACTORY_H__

#include <memory>

#include "rtc/hw/backend_registry.h"

namespace webrtc {
class VideoDecoderFactory;
class VideoEncoderFactory;
}

namespace rflow::rtc::adapt {

using VideoBackendPreferences = rflow::rtc::hw::VideoBackendPreferences;

std::unique_ptr<webrtc::VideoEncoderFactory> CreatePreferredVideoEncoderFactory(
    const VideoBackendPreferences& prefs);

std::unique_ptr<webrtc::VideoDecoderFactory> CreatePreferredVideoDecoderFactory(
    const VideoBackendPreferences& prefs);

}  // namespace rflow::rtc::adapt

#endif  // __RFLOW_CORE_RTC_ADAPT_WEBRTC_CODEC_FACTORY_H__

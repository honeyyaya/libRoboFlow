#ifndef __RFLOW_CORE_RTC_HW_BACKEND_REGISTRY_H__
#define __RFLOW_CORE_RTC_HW_BACKEND_REGISTRY_H__

#include <memory>

namespace webrtc {
class VideoEncoderFactory;
class VideoDecoderFactory;
}

namespace webrtc_demo::hw {

enum class VideoCodecBackend {
  kBuiltin = 0,
  kRockchipMpp = 1,
};

struct VideoBackendPreferences {
  VideoCodecBackend encoder_backend{VideoCodecBackend::kBuiltin};
  VideoCodecBackend decoder_backend{VideoCodecBackend::kBuiltin};
};

std::unique_ptr<webrtc::VideoEncoderFactory> CreatePreferredVideoEncoderFactory(
    const VideoBackendPreferences& prefs);

std::unique_ptr<webrtc::VideoDecoderFactory> CreatePreferredVideoDecoderFactory(
    const VideoBackendPreferences& prefs);

}  // namespace webrtc_demo::hw

#endif  // __RFLOW_CORE_RTC_HW_BACKEND_REGISTRY_H__


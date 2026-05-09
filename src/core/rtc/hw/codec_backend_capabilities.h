#ifndef __RFLOW_CORE_RTC_HW_CODEC_BACKEND_CAPABILITIES_H__
#define __RFLOW_CORE_RTC_HW_CODEC_BACKEND_CAPABILITIES_H__

#include <string>
#include <vector>

namespace rflow::rtc::hw {

enum class ZeroCopyLevel {
  kNone = 0,
  kInputOnly = 1,
  kOutputOnly = 2,
  kFull = 3,
};

struct CodecBackendCapabilities {
  std::string backend_name;
  std::vector<std::string> encoder_codecs;
  std::vector<std::string> decoder_codecs;
  std::vector<std::string> pixel_formats;
  bool supports_dynamic_bitrate{false};
  bool supports_forced_idr{false};
  bool supports_temporal_layers{false};
  ZeroCopyLevel zero_copy_level{ZeroCopyLevel::kNone};
};

}  // namespace rflow::rtc::hw

#endif  // __RFLOW_CORE_RTC_HW_CODEC_BACKEND_CAPABILITIES_H__

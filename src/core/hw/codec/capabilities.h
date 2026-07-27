#ifndef __RFLOW_CORE_HW_CODEC_CAPABILITIES_H__
#define __RFLOW_CORE_HW_CODEC_CAPABILITIES_H__

#include <string>
#include <vector>

namespace rflow::hw {

enum class ZeroCopyLevel {
  kNone = 0,
  kInputOnly = 1,
  kOutputOnly = 2,
  kFull = 3,
};

struct CodecCapabilities {
  std::string backend_name;
  std::vector<std::string> encoder_codecs;
  std::vector<std::string> decoder_codecs;
  std::vector<std::string> pixel_formats;
  bool supports_dynamic_bitrate{false};
  bool supports_forced_idr{false};
  bool supports_temporal_layers{false};
  ZeroCopyLevel zero_copy_level{ZeroCopyLevel::kNone};
};

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_CODEC_CAPABILITIES_H__

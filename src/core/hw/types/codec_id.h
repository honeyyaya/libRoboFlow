#ifndef __RFLOW_CORE_HW_TYPES_CODEC_ID_H__
#define __RFLOW_CORE_HW_TYPES_CODEC_ID_H__

#include <cstdint>

#include "rflow/librflow_common.h"

namespace rflow::hw {

enum class HwCodecId : uint32_t {
  kUnknown = RFLOW_CODEC_UNKNOWN,
  kI420 = RFLOW_CODEC_I420,
  kNv12 = RFLOW_CODEC_NV12,
  kH264 = RFLOW_CODEC_H264,
  kH265 = RFLOW_CODEC_H265,
  kMjpeg = RFLOW_CODEC_MJPEG,
};

inline HwCodecId CodecFromAbi(rflow_codec_t codec) {
  return static_cast<HwCodecId>(codec);
}

inline rflow_codec_t CodecToAbi(HwCodecId codec) {
  return static_cast<rflow_codec_t>(codec);
}

inline const char* CodecName(HwCodecId codec) {
  switch (codec) {
    case HwCodecId::kH264: return "h264";
    case HwCodecId::kH265: return "h265";
    case HwCodecId::kMjpeg: return "mjpeg";
    case HwCodecId::kNv12: return "nv12";
    case HwCodecId::kI420: return "i420";
    default: return "unknown";
  }
}

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_TYPES_CODEC_ID_H__

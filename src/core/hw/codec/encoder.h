#ifndef __RFLOW_CORE_HW_CODEC_ENCODER_H__
#define __RFLOW_CORE_HW_CODEC_ENCODER_H__

#include "hw/codec/capabilities.h"
#include "hw/codec/encoder_callback.h"
#include "hw/frame/encoded_packet.h"
#include "hw/frame/video_frame.h"
#include "hw/types/codec_id.h"
#include "hw/types/error.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace rflow::hw {

enum class HwRateControlMode : uint8_t {
  kVbr = 0,
  kCbr = 1,
};

struct EncoderConfig {
  HwCodecId codec{HwCodecId::kH264};
  uint32_t width{0};
  uint32_t height{0};
  uint32_t fps{30};
  uint32_t target_kbps{0};
  uint32_t min_kbps{0};
  uint32_t max_kbps{0};
  HwRateControlMode rc_mode{HwRateControlMode::kVbr};
  bool prefer_zero_copy_input{true};
  std::unordered_map<std::string, std::string> vendor_opts;
};

class IVideoEncoder {
 public:
  virtual ~IVideoEncoder() = default;

  virtual HwCodecError Init(const EncoderConfig& cfg) = 0;
  virtual void Release() = 0;

  virtual HwCodecError EncodeSync(std::shared_ptr<IVideoFrame> frame,
                                  std::shared_ptr<IEncodedPacket>* out) = 0;
  virtual HwCodecError EncodeAsync(std::shared_ptr<IVideoFrame> frame) = 0;

  virtual void SetCallback(IEncoderCallback* cb) = 0;
  virtual void SetRates(uint32_t target_kbps, uint32_t min_kbps, uint32_t max_kbps) = 0;
  virtual void RequestKeyframe() = 0;

  virtual CodecCapabilities capabilities() const = 0;
};

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_CODEC_ENCODER_H__

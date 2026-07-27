#ifndef __RFLOW_CORE_HW_CODEC_DECODER_H__
#define __RFLOW_CORE_HW_CODEC_DECODER_H__

#include "hw/codec/capabilities.h"
#include "hw/codec/decoder_callback.h"
#include "hw/frame/encoded_packet.h"
#include "hw/frame/video_frame.h"
#include "hw/types/codec_id.h"
#include "hw/types/error.h"
#include "hw/types/pixel_format.h"

#include <cstdint>
#include <memory>

namespace rflow::hw {

struct DecoderConfig {
  HwCodecId codec{HwCodecId::kH264};
  uint32_t max_width{0};
  uint32_t max_height{0};
  HwPixelFormat output_preference{HwPixelFormat::kNv12};
  bool prefer_zero_copy_output{true};
};

class IVideoDecoder {
 public:
  virtual ~IVideoDecoder() = default;

  virtual HwCodecError Init(const DecoderConfig& cfg) = 0;
  virtual void Release() = 0;

  virtual HwCodecError DecodeSync(std::shared_ptr<IEncodedPacket> packet,
                                  std::shared_ptr<IVideoFrame>* out) = 0;
  virtual HwCodecError DecodeAsync(std::shared_ptr<IEncodedPacket> packet) = 0;

  virtual void SetCallback(IDecoderCallback* cb) = 0;
  virtual CodecCapabilities capabilities() const = 0;
};

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_CODEC_DECODER_H__

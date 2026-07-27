#ifndef __RFLOW_CORE_HW_CODEC_ENCODER_CALLBACK_H__
#define __RFLOW_CORE_HW_CODEC_ENCODER_CALLBACK_H__

#include "hw/types/error.h"

#include <memory>

namespace rflow::hw {

class IEncodedPacket;
class IVideoFrame;

class IEncoderCallback {
 public:
  virtual ~IEncoderCallback() = default;
  virtual void OnEncoded(std::shared_ptr<IEncodedPacket> packet,
                         std::shared_ptr<IVideoFrame> source_meta) = 0;
  virtual void OnError(HwCodecError code, const char* message) = 0;
};

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_CODEC_ENCODER_CALLBACK_H__

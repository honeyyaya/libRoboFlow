#ifndef __RFLOW_CORE_HW_CODEC_DECODER_CALLBACK_H__
#define __RFLOW_CORE_HW_CODEC_DECODER_CALLBACK_H__

#include "hw/types/error.h"

#include <memory>

namespace rflow::hw {

class IVideoFrame;

class IDecoderCallback {
 public:
  virtual ~IDecoderCallback() = default;
  virtual void OnDecoded(std::shared_ptr<IVideoFrame> frame) = 0;
  virtual void OnError(HwCodecError code, const char* message) = 0;
};

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_CODEC_DECODER_CALLBACK_H__

#ifndef __RFLOW_CORE_HW_SERVICE_CODEC_SERVICE_H__
#define __RFLOW_CORE_HW_SERVICE_CODEC_SERVICE_H__

#include "hw/codec/decoder.h"
#include "hw/codec/encoder.h"
#include "hw/registry/backend_id.h"
#include "hw/registry/backend_registry.h"
#include "hw/types/codec_id.h"

#include <memory>

namespace rflow::hw {

class CodecService {
 public:
  static std::unique_ptr<IVideoEncoder> CreateEncoder(HwBackendId backend,
                                                      const EncoderConfig& cfg,
                                                      const BackendCreateOptions& opts = {});
  static std::unique_ptr<IVideoDecoder> CreateDecoder(HwBackendId backend,
                                                      const DecoderConfig& cfg,
                                                      const BackendCreateOptions& opts = {});

  static HwBackendId ResolveBackend(bool prefer_hw, HwCodecId codec, bool for_encoder);
};

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_SERVICE_CODEC_SERVICE_H__

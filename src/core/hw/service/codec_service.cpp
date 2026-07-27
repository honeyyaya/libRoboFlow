#include "hw/service/codec_service.h"

namespace rflow::hw {

std::unique_ptr<IVideoEncoder> CodecService::CreateEncoder(HwBackendId backend,
                                                            const EncoderConfig& cfg,
                                                            const BackendCreateOptions& opts) {
  EnsureHwBackendsRegistered();
  const BackendModule* module = BackendRegistry::Instance().Find(backend);
  if (!module || !module->create_encoder) {
    return nullptr;
  }
  if (module->is_available && !module->is_available()) {
    return nullptr;
  }
  return module->create_encoder(cfg, opts);
}

std::unique_ptr<IVideoDecoder> CodecService::CreateDecoder(HwBackendId backend,
                                                            const DecoderConfig& cfg,
                                                            const BackendCreateOptions& opts) {
  EnsureHwBackendsRegistered();
  const BackendModule* module = BackendRegistry::Instance().Find(backend);
  if (!module || !module->create_decoder) {
    return nullptr;
  }
  if (module->is_available && !module->is_available()) {
    return nullptr;
  }
  return module->create_decoder(cfg, opts);
}

HwBackendId CodecService::ResolveBackend(bool prefer_hw, HwCodecId /*codec*/, bool /*for_encoder*/) {
  EnsureHwBackendsRegistered();
  if (!prefer_hw) {
    return HwBackendId::kBuiltin;
  }
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
  const BackendModule* rk = BackendRegistry::Instance().Find(HwBackendId::kRockchipMpp);
  if (rk && (!rk->is_available || rk->is_available())) {
    return HwBackendId::kRockchipMpp;
  }
#endif
  return HwBackendId::kBuiltin;
}

}  // namespace rflow::hw

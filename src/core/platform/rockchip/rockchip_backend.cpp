#include "platform/rockchip/rockchip_backend.h"

#include "platform/rockchip/backend_capabilities.h"
#include "platform/rockchip/mpp/mpp_h264_hw_decoder.h"
#include "platform/rockchip/mpp/mpp_h264_hw_encoder.h"

namespace rflow::hw::rockchip {

namespace {

CodecCapabilities ToHwCapabilities(const rflow::rtc::hw::CodecBackendCapabilities& in) {
  CodecCapabilities out;
  out.backend_name = in.backend_name;
  out.encoder_codecs = in.encoder_codecs;
  out.decoder_codecs = in.decoder_codecs;
  out.pixel_formats = in.pixel_formats;
  out.supports_dynamic_bitrate = in.supports_dynamic_bitrate;
  out.supports_forced_idr = in.supports_forced_idr;
  out.supports_temporal_layers = in.supports_temporal_layers;
  out.zero_copy_level = static_cast<ZeroCopyLevel>(static_cast<int>(in.zero_copy_level));
  return out;
}

}  // namespace

void RegisterRockchipBackend(BackendRegistry& registry) {
  BackendModule module;
  module.id = HwBackendId::kRockchipMpp;
  module.name = "rockchip_mpp";
  module.is_available = [] { return true; };
  module.get_capabilities = [] {
    return ToHwCapabilities(rflow::rtc::hw::rockchip_mpp::GetBackendCapabilities());
  };
  module.create_encoder = [](const EncoderConfig& /*cfg*/, const BackendCreateOptions&) {
    return std::unique_ptr<IVideoEncoder>(std::make_unique<MppH264HwEncoder>());
  };
  module.create_decoder = [](const DecoderConfig& cfg, const BackendCreateOptions&) {
    auto dec = std::make_unique<MppH264HwDecoder>();
    if (!Ok(dec->Init(cfg))) {
      return std::unique_ptr<IVideoDecoder>{};
    }
    return std::unique_ptr<IVideoDecoder>(std::move(dec));
  };
  registry.Register(std::move(module));
}

}  // namespace rflow::hw::rockchip

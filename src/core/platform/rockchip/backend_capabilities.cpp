#include "platform/rockchip/backend_capabilities.h"

namespace rflow::rtc::hw::rockchip_mpp {

CodecBackendCapabilities GetBackendCapabilities() {
  CodecBackendCapabilities caps;
  caps.backend_name = "rockchip_mpp";
  caps.encoder_codecs = {"h264"};
  caps.decoder_codecs = {"h264", "mjpeg"};
  caps.pixel_formats = {"NV12", "I420", "NativeHandle"};
  caps.supports_dynamic_bitrate = true;
  caps.supports_forced_idr = true;
  caps.supports_temporal_layers = false;
  caps.zero_copy_level = ZeroCopyLevel::kInputOnly;
  return caps;
}

}  // namespace rflow::rtc::hw::rockchip_mpp

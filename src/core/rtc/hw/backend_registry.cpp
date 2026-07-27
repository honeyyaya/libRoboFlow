#include "rtc/hw/backend_registry.h"

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "rtc/shims/builtin_video_decoder_recovery.h"
#include "runtime/runtime_knobs.h"

#if defined(WEBRTC_ANDROID)
#include "platform/android/video_decoder_factory.h"
#endif

#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
#include "platform/rockchip/backend_capabilities.h"
#include "platform/rockchip/video_codec_factory.h"
#endif

namespace rflow::rtc::hw {
namespace {

class VideoBackendRegistryImpl {
 public:
  void Register(VideoBackendDescriptor desc) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&](const VideoBackendDescriptor& e) {
                             return e.backend == desc.backend;
                           });
    if (it != entries_.end()) {
      *it = std::move(desc);
    } else {
      entries_.push_back(std::move(desc));
    }
  }

  const VideoBackendDescriptor* Find(VideoCodecBackend backend) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& e : entries_) {
      if (e.backend == backend) return &e;
    }
    return nullptr;
  }

  std::vector<CodecBackendCapabilities> SnapshotCapabilities() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<CodecBackendCapabilities> out;
    out.reserve(entries_.size());
    for (const auto& e : entries_) {
      if (e.get_capabilities) out.push_back(e.get_capabilities());
    }
    return out;
  }

 private:
  mutable std::mutex                     mu_;
  std::vector<VideoBackendDescriptor>    entries_;
};

VideoBackendRegistryImpl& GetRegistryImpl() {
  static VideoBackendRegistryImpl r;
  return r;
}

VideoBackendDescriptor MakeBuiltinDescriptor() {
  VideoBackendDescriptor d;
  d.backend = VideoCodecBackend::kBuiltin;
  d.name    = "builtin";
  d.create_encoder_factory = [] { return webrtc::CreateBuiltinVideoEncoderFactory(); };
  d.create_decoder_factory = [] { return rflow::rtc::shims::CreateRecoveringBuiltinVideoDecoderFactory(); };
  d.get_capabilities = [] {
    CodecBackendCapabilities c;
    c.backend_name              = "builtin";
    c.encoder_codecs            = {"vp8", "vp9", "h264", "av1"};
    c.decoder_codecs            = {"vp8", "vp9", "h264", "av1"};
    c.pixel_formats             = {"I420", "NV12"};
    c.supports_dynamic_bitrate  = true;
    c.supports_forced_idr       = true;
    c.zero_copy_level           = ZeroCopyLevel::kNone;
    return c;
  };
  return d;
}

#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
VideoBackendDescriptor MakeRockchipMppDescriptor() {
  VideoBackendDescriptor d;
  d.backend = VideoCodecBackend::kRockchipMpp;
  d.name    = "rockchip_mpp";
  d.create_encoder_factory = [] { return rflow::rtc::hw::rockchip_mpp::CreateVideoEncoderFactory(false); };
  d.create_decoder_factory = [] { return rflow::rtc::hw::rockchip_mpp::CreateVideoDecoderFactory(); };
  d.get_capabilities       = [] { return rflow::rtc::hw::rockchip_mpp::GetBackendCapabilities(); };
  return d;
}
#endif

#if defined(WEBRTC_ANDROID)
VideoBackendDescriptor MakeAndroidMediaCodecDescriptor() {
  VideoBackendDescriptor d;
  d.backend = VideoCodecBackend::kAndroidMediaCodec;
  d.name    = "android_mediacodec";
  // 当前 Android 端只接管解码（编码仍走 builtin / 上层不区分）。
  d.create_decoder_factory = [] { return rflow::rtc::CreateAndroidHwOrBuiltinVideoDecoderFactory(); };
  d.get_capabilities = [] {
    CodecBackendCapabilities c;
    c.backend_name              = "android_mediacodec";
    c.encoder_codecs            = {};
    c.decoder_codecs            = {"h264"};
    c.pixel_formats             = {"NV12"};
    c.supports_dynamic_bitrate  = false;
    c.supports_forced_idr       = false;
    c.zero_copy_level           = ZeroCopyLevel::kNone;
    return c;
  };
  return d;
}
#endif

void EnsureBackendsRegistered() {
  static std::once_flag once;
  std::call_once(once, [] {
    GetRegistryImpl().Register(MakeBuiltinDescriptor());
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
    GetRegistryImpl().Register(MakeRockchipMppDescriptor());
#endif
#if defined(WEBRTC_ANDROID)
    GetRegistryImpl().Register(MakeAndroidMediaCodecDescriptor());
#endif
  });
}

}  // namespace

// ---- public API -----------------------------------------------------------

VideoBackendRegistry& VideoBackendRegistry::Instance() {
  static VideoBackendRegistry g;
  return g;
}

void VideoBackendRegistry::Register(VideoBackendDescriptor desc) {
  GetRegistryImpl().Register(std::move(desc));
}

const VideoBackendDescriptor* VideoBackendRegistry::Find(VideoCodecBackend backend) const {
  return GetRegistryImpl().Find(backend);
}

std::vector<CodecBackendCapabilities> VideoBackendRegistry::SnapshotCapabilities() const {
  return GetRegistryImpl().SnapshotCapabilities();
}

std::unique_ptr<webrtc::VideoEncoderFactory> CreatePreferredVideoEncoderFactory(
    const VideoBackendPreferences& prefs) {
  EnsureBackendsRegistered();
  if (prefs.encoder_backend != VideoCodecBackend::kBuiltin) {
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
    if (prefs.encoder_backend == VideoCodecBackend::kRockchipMpp) {
      if (const auto* d = GetRegistryImpl().Find(VideoCodecBackend::kRockchipMpp)) {
        const bool runtime_off = d->encoder_disabled_at_runtime && d->encoder_disabled_at_runtime();
        if (!runtime_off) {
          return rflow::rtc::hw::rockchip_mpp::CreateVideoEncoderFactory(
              prefs.rockchip_h264_encoder_mpp_rc_cbr);
        }
      }
    }
#endif
    if (const auto* d = GetRegistryImpl().Find(prefs.encoder_backend)) {
      const bool runtime_off = d->encoder_disabled_at_runtime && d->encoder_disabled_at_runtime();
      if (!runtime_off && d->create_encoder_factory) {
        if (auto f = d->create_encoder_factory()) {
          return f;
        }
      }
    }
  }
  if (const auto* builtin = GetRegistryImpl().Find(VideoCodecBackend::kBuiltin)) {
    if (builtin->create_encoder_factory) return builtin->create_encoder_factory();
  }
  return webrtc::CreateBuiltinVideoEncoderFactory();
}

std::unique_ptr<webrtc::VideoDecoderFactory> CreatePreferredVideoDecoderFactory(
    const VideoBackendPreferences& prefs) {
  EnsureBackendsRegistered();
  if (prefs.decoder_backend != VideoCodecBackend::kBuiltin) {
    if (const auto* d = GetRegistryImpl().Find(prefs.decoder_backend)) {
      const bool runtime_off = d->decoder_disabled_at_runtime && d->decoder_disabled_at_runtime();
      if (!runtime_off && d->create_decoder_factory) {
        if (auto f = d->create_decoder_factory()) {
          return f;
        }
      }
    }
  }
  if (const auto* builtin = GetRegistryImpl().Find(VideoCodecBackend::kBuiltin)) {
    if (builtin->create_decoder_factory) return builtin->create_decoder_factory();
  }
  return webrtc::CreateBuiltinVideoDecoderFactory();
}

std::vector<CodecBackendCapabilities> GetAvailableCodecBackendCapabilities() {
  EnsureBackendsRegistered();
  return GetRegistryImpl().SnapshotCapabilities();
}

}  // namespace rflow::rtc::hw

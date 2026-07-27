#include "rtc/adapt/webrtc_codec_factory.h"

#include "rtc/adapt/webrtc_encoder_adapter.h"
#include "rtc/hw/backend_registry.h"

#include <optional>

#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "media/engine/internal_encoder_factory.h"
#include "modules/video_coding/include/video_error_codes.h"

#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
#include "hw/codec/encoder.h"
#include "hw/registry/backend_id.h"
#include "hw/service/codec_service.h"
#endif

namespace rflow::rtc::adapt {
namespace {

bool IsH264FormatName(const std::string& name) {
  if (name.size() != 4) {
    return false;
  }
  return (name[0] == 'H' || name[0] == 'h') && name[1] == '2' && name[2] == '6' && name[3] == '4';
}

#if defined(RFLOW_HAVE_ROCKCHIP_MPP)

rflow::hw::EncoderConfig ToHwEncoderConfig(const webrtc::VideoCodec& codec, bool mpp_rc_cbr) {
  rflow::hw::EncoderConfig cfg;
  cfg.codec = rflow::hw::HwCodecId::kH264;
  cfg.width = static_cast<uint32_t>(codec.width);
  cfg.height = static_cast<uint32_t>(codec.height);
  cfg.fps = codec.maxFramerate;
  cfg.target_kbps = codec.startBitrate;
  cfg.min_kbps = codec.minBitrate;
  cfg.max_kbps = codec.maxBitrate;
  cfg.rc_mode = mpp_rc_cbr ? rflow::hw::HwRateControlMode::kCbr : rflow::hw::HwRateControlMode::kVbr;
  cfg.prefer_zero_copy_input = true;
  return cfg;
}

/// H264：CodecService + WebrtcEncoderAdapter 主路径，Init/Encode 失败时懒加载 OpenH264。
class H264HwCodecServiceWithLazySwFallbackEncoder final : public webrtc::VideoEncoder {
 public:
  H264HwCodecServiceWithLazySwFallbackEncoder(const webrtc::Environment& env,
                                              const webrtc::SdpVideoFormat& format,
                                              bool mpp_rc_cbr,
                                              webrtc::InternalEncoderFactory* sw_factory)
      : env_(env), format_(format), mpp_rc_cbr_(mpp_rc_cbr), sw_factory_(sw_factory) {}

  webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override {
    if (active_) {
      return active_->GetEncoderInfo();
    }
    if (hw_encoder_) {
      return hw_encoder_->GetEncoderInfo();
    }
    webrtc::VideoEncoder::EncoderInfo info;
    info.implementation_name = "rflow_hw_codec_service";
    info.supports_native_handle = true;
    return info;
  }

  void SetFecControllerOverride(webrtc::FecControllerOverride* o) override {
    fec_override_ = o;
    if (hw_encoder_) {
      hw_encoder_->SetFecControllerOverride(o);
    }
    if (sw_encoder_) {
      sw_encoder_->SetFecControllerOverride(o);
    }
  }

  int32_t RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) override {
    callback_ = callback;
    if (hw_encoder_) {
      hw_encoder_->RegisterEncodeCompleteCallback(callback);
    }
    if (sw_encoder_) {
      sw_encoder_->RegisterEncodeCompleteCallback(callback);
    }
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int InitEncode(const webrtc::VideoCodec* codec_settings,
                 const webrtc::VideoEncoder::Settings& settings) override {
    codec_settings_ = *codec_settings;
    encoder_settings_ = settings;

    const rflow::hw::EncoderConfig cfg = ToHwEncoderConfig(*codec_settings, mpp_rc_cbr_);
    auto hw = rflow::hw::CodecService::CreateEncoder(rflow::hw::HwBackendId::kRockchipMpp, cfg);
    if (hw) {
      hw_encoder_ = CreateWebrtcEncoderAdapter(std::move(hw));
      if (hw_encoder_) {
        hw_encoder_->SetFecControllerOverride(fec_override_);
        if (callback_) {
          hw_encoder_->RegisterEncodeCompleteCallback(callback_);
        }
        const int hw_rc = hw_encoder_->InitEncode(codec_settings, settings);
        if (hw_rc == WEBRTC_VIDEO_CODEC_OK) {
          active_ = hw_encoder_.get();
          return hw_rc;
        }
        hw_encoder_.reset();
      }
    }
    return SwitchToSoftwareEncoder(codec_settings, settings);
  }

  int32_t Release() override {
    if (sw_encoder_) {
      sw_encoder_->Release();
      sw_encoder_.reset();
    }
    if (hw_encoder_) {
      hw_encoder_->Release();
      hw_encoder_.reset();
    }
    active_ = nullptr;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* frame_types) override {
    if (!active_) {
      active_ = hw_encoder_.get();
    }
    if (!active_) {
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    const int32_t rc = active_->Encode(frame, frame_types);
    if (rc == WEBRTC_VIDEO_CODEC_OK || active_ != hw_encoder_.get()) {
      return rc;
    }
    if (rc != WEBRTC_VIDEO_CODEC_ERROR || !codec_settings_.has_value() || !encoder_settings_.has_value()) {
      return rc;
    }
    if (SwitchToSoftwareEncoder(&*codec_settings_, *encoder_settings_) != WEBRTC_VIDEO_CODEC_OK) {
      return rc;
    }
    return active_->Encode(frame, frame_types);
  }

  void SetRates(const webrtc::VideoEncoder::RateControlParameters& parameters) override {
    if (active_) {
      active_->SetRates(parameters);
    } else if (hw_encoder_) {
      hw_encoder_->SetRates(parameters);
    }
  }

  void OnPacketLossRateUpdate(float packet_loss_rate) override {
    if (hw_encoder_) {
      hw_encoder_->OnPacketLossRateUpdate(packet_loss_rate);
    }
    if (sw_encoder_) {
      sw_encoder_->OnPacketLossRateUpdate(packet_loss_rate);
    }
  }

  void OnRttUpdate(int64_t rtt_ms) override {
    if (hw_encoder_) {
      hw_encoder_->OnRttUpdate(rtt_ms);
    }
    if (sw_encoder_) {
      sw_encoder_->OnRttUpdate(rtt_ms);
    }
  }

  void OnLossNotification(const webrtc::VideoEncoder::LossNotification& loss_notification) override {
    if (hw_encoder_) {
      hw_encoder_->OnLossNotification(loss_notification);
    }
    if (sw_encoder_) {
      sw_encoder_->OnLossNotification(loss_notification);
    }
  }

 private:
  int SwitchToSoftwareEncoder(const webrtc::VideoCodec* codec_settings,
                              const webrtc::VideoEncoder::Settings& settings) {
    if (!sw_factory_) {
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    sw_encoder_ = sw_factory_->Create(env_, format_);
    if (!sw_encoder_) {
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    sw_encoder_->SetFecControllerOverride(fec_override_);
    if (callback_) {
      sw_encoder_->RegisterEncodeCompleteCallback(callback_);
    }
    const int sw_rc = sw_encoder_->InitEncode(codec_settings, settings);
    if (sw_rc != WEBRTC_VIDEO_CODEC_OK) {
      sw_encoder_.reset();
      return sw_rc;
    }
    active_ = sw_encoder_.get();
    return WEBRTC_VIDEO_CODEC_OK;
  }

  const webrtc::Environment& env_;
  webrtc::SdpVideoFormat format_;
  bool mpp_rc_cbr_;
  webrtc::InternalEncoderFactory* sw_factory_;
  std::unique_ptr<webrtc::VideoEncoder> hw_encoder_;
  std::unique_ptr<webrtc::VideoEncoder> sw_encoder_;
  webrtc::VideoEncoder* active_{nullptr};
  webrtc::FecControllerOverride* fec_override_{nullptr};
  webrtc::EncodedImageCallback* callback_{nullptr};
  std::optional<webrtc::VideoCodec> codec_settings_;
  std::optional<webrtc::VideoEncoder::Settings> encoder_settings_;
};

class AdaptRockchipVideoEncoderFactory final : public webrtc::VideoEncoderFactory {
 public:
  explicit AdaptRockchipVideoEncoderFactory(bool mpp_rc_cbr)
      : mpp_rc_cbr_(mpp_rc_cbr), builtin_(webrtc::CreateBuiltinVideoEncoderFactory()) {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    return builtin_->GetSupportedFormats();
  }

  webrtc::VideoEncoderFactory::CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat& format,
                                                              std::optional<std::string> scalability_mode)
      const override {
    return builtin_->QueryCodecSupport(format, scalability_mode);
  }

  std::unique_ptr<webrtc::VideoEncoder> Create(const webrtc::Environment& env,
                                               const webrtc::SdpVideoFormat& format) override {
    if (!IsH264FormatName(format.name)) {
      return builtin_->Create(env, format);
    }
    return std::make_unique<H264HwCodecServiceWithLazySwFallbackEncoder>(env, format, mpp_rc_cbr_,
                                                                         &internal_fallback_);
  }

 private:
  bool mpp_rc_cbr_;
  webrtc::InternalEncoderFactory internal_fallback_;
  std::unique_ptr<webrtc::VideoEncoderFactory> builtin_;
};

#endif  // RFLOW_HAVE_ROCKCHIP_MPP

}  // namespace

std::unique_ptr<webrtc::VideoEncoderFactory> CreatePreferredVideoEncoderFactory(
    const VideoBackendPreferences& prefs) {
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
  if (prefs.encoder_backend == rflow::rtc::hw::VideoCodecBackend::kRockchipMpp) {
    return std::make_unique<AdaptRockchipVideoEncoderFactory>(prefs.rockchip_h264_encoder_mpp_rc_cbr);
  }
#endif
  return rflow::rtc::hw::CreatePreferredVideoEncoderFactory(prefs);
}

std::unique_ptr<webrtc::VideoDecoderFactory> CreatePreferredVideoDecoderFactory(
    const VideoBackendPreferences& prefs) {
  return rflow::rtc::hw::CreatePreferredVideoDecoderFactory(prefs);
}

}  // namespace rflow::rtc::adapt

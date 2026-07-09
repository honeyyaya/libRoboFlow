#include "platform/rockchip/video_encoder_factory.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "api/environment/environment.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "media/engine/internal_encoder_factory.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "modules/video_coding/include/video_error_codes.h"

#include "platform/rockchip/h264_encoder.h"

namespace rflow::rtc::hw::rockchip_mpp {

namespace {

bool IsH264FormatName(const std::string& name) {
    if (name.size() != 4) {
        return false;
    }
    return (name[0] == 'H' || name[0] == 'h') && name[1] == '2' && name[2] == '6' && name[3] == '4';
}

// Single-stream H264 encoder: MPP hardware primary + lazy OpenH264 fallback.
// Avoids SimulcastEncoderAdapter / VideoEncoderSoftwareFallbackWrapper, which query
// an uninitialized SW encoder during GetEncoderInfo before InitEncode.
class H264HwWithLazySwFallbackEncoder final : public webrtc::VideoEncoder {
public:
    H264HwWithLazySwFallbackEncoder(const webrtc::Environment& env,
                                    const webrtc::SdpVideoFormat& format,
                                    bool mpp_rc_cbr,
                                    webrtc::InternalEncoderFactory* sw_factory)
        : env_(env),
          format_(format),
          h264_settings_(webrtc::H264EncoderSettings::Parse(format)),
          mpp_rc_cbr_(mpp_rc_cbr),
          sw_factory_(sw_factory),
          hw_encoder_(std::make_unique<RkMppH264Encoder>(env_, h264_settings_, mpp_rc_cbr_)) {}

    webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override {
        return hw_encoder_->GetEncoderInfo();
    }

    void SetFecControllerOverride(webrtc::FecControllerOverride* o) override {
        fec_override_ = o;
        hw_encoder_->SetFecControllerOverride(o);
        if (sw_encoder_) {
            sw_encoder_->SetFecControllerOverride(o);
        }
    }

    int32_t RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) override {
        callback_ = callback;
        hw_encoder_->RegisterEncodeCompleteCallback(callback);
        if (sw_encoder_) {
            sw_encoder_->RegisterEncodeCompleteCallback(callback);
        }
        return WEBRTC_VIDEO_CODEC_OK;
    }

    int InitEncode(const webrtc::VideoCodec* codec_settings,
                   const webrtc::VideoEncoder::Settings& settings) override {
        codec_settings_ = *codec_settings;
        encoder_settings_ = settings;
        const int hw_rc = hw_encoder_->InitEncode(codec_settings, settings);
        if (hw_rc == WEBRTC_VIDEO_CODEC_OK) {
            active_ = hw_encoder_.get();
            return hw_rc;
        }
        return SwitchToSoftwareEncoder(codec_settings, settings);
    }

    int32_t Release() override {
        if (sw_encoder_) {
            sw_encoder_->Release();
        }
        active_ = nullptr;
        return hw_encoder_->Release();
    }

    int32_t Encode(const webrtc::VideoFrame& frame,
                   const std::vector<webrtc::VideoFrameType>* frame_types) override {
        if (!active_) {
            active_ = hw_encoder_.get();
        }
        const int32_t rc = active_->Encode(frame, frame_types);
        if (rc == WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE && active_ == hw_encoder_.get() &&
            codec_settings_.has_value() && encoder_settings_.has_value()) {
            if (SwitchToSoftwareEncoder(&*codec_settings_, *encoder_settings_) != WEBRTC_VIDEO_CODEC_OK) {
                return rc;
            }
            return active_->Encode(frame, frame_types);
        }
        return rc;
    }

    void SetRates(const webrtc::VideoEncoder::RateControlParameters& parameters) override {
        if (active_) {
            active_->SetRates(parameters);
        } else {
            hw_encoder_->SetRates(parameters);
        }
    }

    void OnPacketLossRateUpdate(float packet_loss_rate) override {
        hw_encoder_->OnPacketLossRateUpdate(packet_loss_rate);
        if (sw_encoder_) {
            sw_encoder_->OnPacketLossRateUpdate(packet_loss_rate);
        }
    }

    void OnRttUpdate(int64_t rtt_ms) override {
        hw_encoder_->OnRttUpdate(rtt_ms);
        if (sw_encoder_) {
            sw_encoder_->OnRttUpdate(rtt_ms);
        }
    }

    void OnLossNotification(const webrtc::VideoEncoder::LossNotification& loss_notification) override {
        hw_encoder_->OnLossNotification(loss_notification);
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
    webrtc::H264EncoderSettings h264_settings_;
    bool mpp_rc_cbr_;
    webrtc::InternalEncoderFactory* sw_factory_;
    std::unique_ptr<RkMppH264Encoder> hw_encoder_;
    std::unique_ptr<webrtc::VideoEncoder> sw_encoder_;
    webrtc::VideoEncoder* active_{nullptr};
    webrtc::FecControllerOverride* fec_override_{nullptr};
    webrtc::EncodedImageCallback* callback_{nullptr};
    std::optional<webrtc::VideoCodec> codec_settings_;
    std::optional<webrtc::VideoEncoder::Settings> encoder_settings_;
};

class PreferredVideoEncoderFactory final : public webrtc::VideoEncoderFactory {
public:
    explicit PreferredVideoEncoderFactory(bool mpp_rc_cbr)
        : mpp_rc_cbr_(mpp_rc_cbr),
          builtin_(webrtc::CreateBuiltinVideoEncoderFactory()) {}

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
        return std::make_unique<H264HwWithLazySwFallbackEncoder>(env, format, mpp_rc_cbr_, &internal_fallback_);
    }

private:
    bool mpp_rc_cbr_;
    webrtc::InternalEncoderFactory internal_fallback_;
    std::unique_ptr<webrtc::VideoEncoderFactory> builtin_;
};

}  // namespace

std::unique_ptr<webrtc::VideoEncoderFactory> CreateVideoEncoderFactory(bool mpp_rc_cbr) {
    return std::make_unique<PreferredVideoEncoderFactory>(mpp_rc_cbr);
}

}  // namespace rflow::rtc::hw::rockchip_mpp

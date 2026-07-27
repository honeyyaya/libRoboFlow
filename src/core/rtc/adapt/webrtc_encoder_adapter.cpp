#include "rtc/adapt/webrtc_encoder_adapter.h"

#include "hw/frame/webrtc_frame_bridge.h"

#include "api/video/encoded_image.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/include/video_error_codes.h"

namespace rflow::rtc::adapt {
namespace {

class WebrtcEncoderAdapter;

class HwEncoderCallbackBridge final : public rflow::hw::IEncoderCallback {
 public:
  explicit HwEncoderCallbackBridge(WebrtcEncoderAdapter* owner) : owner_(owner) {}

  void OnEncoded(std::shared_ptr<rflow::hw::IEncodedPacket> packet,
                 std::shared_ptr<rflow::hw::IVideoFrame> source_meta) override;

  void OnError(rflow::hw::HwCodecError code, const char* message) override;

 private:
  WebrtcEncoderAdapter* owner_;
};

class WebrtcEncoderAdapter final : public webrtc::VideoEncoder {
 public:
  explicit WebrtcEncoderAdapter(std::unique_ptr<rflow::hw::IVideoEncoder> encoder)
      : encoder_(std::move(encoder)), hw_callback_bridge_(this) {}

  int InitEncode(const webrtc::VideoCodec* codec_settings,
                 const webrtc::VideoEncoder::Settings& settings) override {
    (void)settings;
    if (!encoder_ || !codec_settings) {
      return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }
    rflow::hw::EncoderConfig cfg;
    cfg.codec = rflow::hw::HwCodecId::kH264;
    cfg.width = static_cast<uint32_t>(codec_settings->width);
    cfg.height = static_cast<uint32_t>(codec_settings->height);
    cfg.fps = codec_settings->maxFramerate;
    cfg.target_kbps = codec_settings->startBitrate;
    cfg.min_kbps = codec_settings->minBitrate;
    cfg.max_kbps = codec_settings->maxBitrate;
    encoder_->SetCallback(&hw_callback_bridge_);
    return rflow::hw::Ok(encoder_->Init(cfg)) ? WEBRTC_VIDEO_CODEC_OK : WEBRTC_VIDEO_CODEC_ERROR;
  }

  int32_t RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) override {
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Release() override {
    if (encoder_) {
      encoder_->SetCallback(nullptr);
      encoder_->Release();
    }
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* frame_types) override {
    if (!encoder_ || !callback_) {
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    if (frame_types) {
      for (const webrtc::VideoFrameType type : *frame_types) {
        if (type == webrtc::VideoFrameType::kVideoFrameKey) {
          encoder_->RequestKeyframe();
          break;
        }
      }
    }
    auto hw_frame = rflow::hw::FromWebrtcVideoFrame(frame);
    if (!hw_frame || !hw_frame->buffer()) {
      return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }
    pending_width_ = static_cast<uint32_t>(frame.width());
    pending_height_ = static_cast<uint32_t>(frame.height());
    return rflow::hw::Ok(encoder_->EncodeAsync(std::move(hw_frame))) ? WEBRTC_VIDEO_CODEC_OK
                                                                      : WEBRTC_VIDEO_CODEC_ERROR;
  }

  void SetRates(const webrtc::VideoEncoder::RateControlParameters& parameters) override {
    if (!encoder_) {
      return;
    }
    const int bps = parameters.bitrate.GetBitrate(0, 0);
    const uint32_t kbps = bps > 0 ? static_cast<uint32_t>(bps / 1000) : 0;
    encoder_->SetRates(kbps, kbps, kbps);
  }

  webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override {
    webrtc::VideoEncoder::EncoderInfo info;
    info.implementation_name = "rflow_hw_encoder_adapter";
    info.supports_native_handle = true;
    return info;
  }

  void DeliverEncoded(std::shared_ptr<rflow::hw::IEncodedPacket> packet,
                      std::shared_ptr<rflow::hw::IVideoFrame> /*source_meta*/) {
    if (!callback_ || !packet) {
      return;
    }
    webrtc::EncodedImage image;
    image.SetEncodedData(webrtc::EncodedImageBuffer::Create(packet->data(), packet->size()));
    image.SetFrameType(packet->is_keyframe() ? webrtc::VideoFrameType::kVideoFrameKey
                                             : webrtc::VideoFrameType::kVideoFrameDelta);
    image._encodedWidth = pending_width_;
    image._encodedHeight = pending_height_;
    callback_->OnEncodedImage(image, nullptr);
  }

  void DeliverError(rflow::hw::HwCodecError /*code*/, const char* /*message*/) {
    last_encode_failed_ = true;
  }

  bool LastEncodeFailed() const { return last_encode_failed_; }

  void ClearEncodeFailure() { last_encode_failed_ = false; }

 private:
  std::unique_ptr<rflow::hw::IVideoEncoder> encoder_;
  HwEncoderCallbackBridge hw_callback_bridge_;
  webrtc::EncodedImageCallback* callback_{nullptr};
  uint32_t pending_width_{0};
  uint32_t pending_height_{0};
  bool last_encode_failed_{false};
};

void HwEncoderCallbackBridge::OnEncoded(std::shared_ptr<rflow::hw::IEncodedPacket> packet,
                                        std::shared_ptr<rflow::hw::IVideoFrame> source_meta) {
  if (owner_) {
    owner_->ClearEncodeFailure();
    owner_->DeliverEncoded(std::move(packet), std::move(source_meta));
  }
}

void HwEncoderCallbackBridge::OnError(rflow::hw::HwCodecError code, const char* message) {
  if (owner_) {
    owner_->DeliverError(code, message);
  }
}

}  // namespace

std::unique_ptr<webrtc::VideoEncoder> CreateWebrtcEncoderAdapter(
    std::unique_ptr<rflow::hw::IVideoEncoder> encoder) {
  if (!encoder) {
    return nullptr;
  }
  return std::make_unique<WebrtcEncoderAdapter>(std::move(encoder));
}

}  // namespace rflow::rtc::adapt

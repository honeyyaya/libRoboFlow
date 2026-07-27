#include "platform/rockchip/mpp/mpp_h264_hw_decoder.h"

#include "hw/frame/simple_video_frame.h"
#include "hw/frame/webrtc_frame_bridge.h"
#include "platform/rockchip/backend_capabilities.h"
#include "platform/rockchip/h264_decoder.h"

#include "api/environment/environment_factory.h"
#include "api/video/encoded_image.h"
#include "api/video_codecs/video_decoder.h"
#include "modules/video_coding/include/video_error_codes.h"

#include <chrono>
#include <condition_variable>
#include <vector>

namespace rflow::hw::rockchip {
namespace {

CodecCapabilities RockchipCaps() {
  const auto rtc_caps = rflow::rtc::hw::rockchip_mpp::GetBackendCapabilities();
  CodecCapabilities c;
  c.backend_name = rtc_caps.backend_name;
  c.encoder_codecs = rtc_caps.encoder_codecs;
  c.decoder_codecs = rtc_caps.decoder_codecs;
  c.pixel_formats = rtc_caps.pixel_formats;
  c.supports_dynamic_bitrate = rtc_caps.supports_dynamic_bitrate;
  c.supports_forced_idr = rtc_caps.supports_forced_idr;
  c.supports_temporal_layers = rtc_caps.supports_temporal_layers;
  c.zero_copy_level = static_cast<ZeroCopyLevel>(static_cast<int>(rtc_caps.zero_copy_level));
  return c;
}

}  // namespace

class MppH264HwDecoder::DecodeCompleteBridge final : public webrtc::DecodedImageCallback {
 public:
  explicit DecodeCompleteBridge(MppH264HwDecoder* owner) : owner_(owner) {}

  int32_t Decoded(webrtc::VideoFrame& decoded_image) override {
    if (owner_) {
      owner_->OnWebrtcDecoded(decoded_image);
    }
    return WEBRTC_VIDEO_CODEC_OK;
  }

 private:
  MppH264HwDecoder* owner_;
};

MppH264HwDecoder::MppH264HwDecoder() : env_(webrtc::CreateEnvironment()) {}

MppH264HwDecoder::~MppH264HwDecoder() { Release(); }

HwCodecError MppH264HwDecoder::Init(const DecoderConfig& cfg) {
  if (cfg.codec != HwCodecId::kH264) {
    return HwCodecError::kInvalidArgument;
  }
  Release();
  config_ = cfg;
  decoder_ = std::make_unique<rflow::rtc::hw::rockchip_mpp::H264Decoder>(env_);
  bridge_ = std::make_unique<DecodeCompleteBridge>(this);
  decoder_->RegisterDecodeCompleteCallback(bridge_.get());
  if (!decoder_->Configure({})) {
    decoder_.reset();
    bridge_.reset();
    return HwCodecError::kHardwareFailure;
  }
  initialized_ = true;
  return HwCodecError::kOk;
}

void MppH264HwDecoder::Release() {
  if (decoder_) {
    decoder_->Release();
    decoder_.reset();
  }
  bridge_.reset();
  initialized_ = false;
}

HwCodecError MppH264HwDecoder::DecodeSync(std::shared_ptr<IEncodedPacket> packet,
                                         std::shared_ptr<IVideoFrame>* out) {
  if (!out) {
    return HwCodecError::kInvalidArgument;
  }
  struct SyncState {
    std::mutex mu;
    std::condition_variable cv;
    std::shared_ptr<IVideoFrame> frame;
    bool done{false};
  } state;

  class SyncCb final : public IDecoderCallback {
   public:
    explicit SyncCb(SyncState* s) : s_(s) {}
    void OnDecoded(std::shared_ptr<IVideoFrame> frame) override {
      std::lock_guard<std::mutex> lk(s_->mu);
      s_->frame = std::move(frame);
      s_->done = true;
      s_->cv.notify_one();
    }
    void OnError(HwCodecError, const char*) override {
      std::lock_guard<std::mutex> lk(s_->mu);
      s_->done = true;
      s_->cv.notify_one();
    }

   private:
    SyncState* s_;
  } sync_cb(&state);

  IDecoderCallback* prev = callback_;
  SetCallback(&sync_cb);
  const HwCodecError rc = DecodeAsync(std::move(packet));
  if (rc != HwCodecError::kOk) {
    SetCallback(prev);
    return rc;
  }
  {
    std::unique_lock<std::mutex> lk(state.mu);
    state.cv.wait_for(lk, std::chrono::seconds(2), [&] { return state.done; });
  }
  SetCallback(prev);
  if (!state.frame) {
    return HwCodecError::kTimeout;
  }
  *out = std::move(state.frame);
  return HwCodecError::kOk;
}

HwCodecError MppH264HwDecoder::DecodeAsync(std::shared_ptr<IEncodedPacket> packet) {
  if (!initialized_ || !decoder_ || !packet || !packet->data() || packet->size() == 0) {
    return HwCodecError::kNotInitialized;
  }
  webrtc::EncodedImage image;
  image.SetEncodedData(webrtc::EncodedImageBuffer::Create(packet->data(), packet->size()));
  image.SetFrameType(packet->is_keyframe() ? webrtc::VideoFrameType::kVideoFrameKey
                                           : webrtc::VideoFrameType::kVideoFrameDelta);
  if (decoder_->Decode(image, false, 0) != WEBRTC_VIDEO_CODEC_OK) {
    return HwCodecError::kHardwareFailure;
  }
  return HwCodecError::kOk;
}

void MppH264HwDecoder::SetCallback(IDecoderCallback* cb) {
  std::lock_guard<std::mutex> lk(mu_);
  callback_ = cb;
}

CodecCapabilities MppH264HwDecoder::capabilities() const { return RockchipCaps(); }

void MppH264HwDecoder::OnWebrtcDecoded(const webrtc::VideoFrame& frame) {
  IDecoderCallback* cb = nullptr;
  {
    std::lock_guard<std::mutex> lk(mu_);
    cb = callback_;
  }
  if (!cb) {
    return;
  }
  auto hw_frame = FromWebrtcVideoFrame(frame);
  if (!hw_frame) {
    cb->OnError(HwCodecError::kHardwareFailure, "decode output not mapped to hw frame");
    return;
  }
  cb->OnDecoded(std::move(hw_frame));
}

}  // namespace rflow::hw::rockchip

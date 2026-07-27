#include "platform/rockchip/mpp/mpp_h264_hw_encoder.h"

#include "hw/frame/webrtc_frame_bridge.h"
#include "platform/rockchip/backend_capabilities.h"
#include "platform/rockchip/h264_encoder.h"

#include "api/environment/environment_factory.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video/encoded_image.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
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

class MppH264HwEncoder::EncodeCompleteBridge final : public webrtc::EncodedImageCallback {
 public:
  explicit EncodeCompleteBridge(MppH264HwEncoder* owner) : owner_(owner) {}

  webrtc::EncodedImageCallback::Result OnEncodedImage(const webrtc::EncodedImage& encoded_image,
                                                      const webrtc::CodecSpecificInfo* /*codec_info*/) override {
    if (!owner_) {
      return Result(Result::ERROR_SEND_FAILED, 0);
    }
    owner_->OnWebrtcEncoded(encoded_image);
    return Result(Result::OK, 0);
  }

 private:
  MppH264HwEncoder* owner_;
};

MppH264HwEncoder::MppH264HwEncoder() : env_(webrtc::CreateEnvironment()) {}

MppH264HwEncoder::~MppH264HwEncoder() { Release(); }

HwCodecError MppH264HwEncoder::Init(const EncoderConfig& cfg) {
  if (cfg.codec != HwCodecId::kH264 || cfg.width == 0 || cfg.height == 0) {
    return HwCodecError::kInvalidArgument;
  }
  Release();
  config_ = cfg;
  const bool cbr = cfg.rc_mode == HwRateControlMode::kCbr;
  webrtc::SdpVideoFormat format("H264");
  const webrtc::H264EncoderSettings settings = webrtc::H264EncoderSettings::Parse(format);
  encoder_ = std::make_unique<rflow::rtc::hw::rockchip_mpp::RkMppH264Encoder>(env_, settings, cbr);
  bridge_ = std::make_unique<EncodeCompleteBridge>(this);
  encoder_->RegisterEncodeCompleteCallback(bridge_.get());

  webrtc::VideoCodec codec{};
  codec.codecType = webrtc::kVideoCodecH264;
  codec.width = static_cast<int>(cfg.width);
  codec.height = static_cast<int>(cfg.height);
  codec.maxFramerate = static_cast<uint32_t>(cfg.fps > 0 ? cfg.fps : 30);
  codec.startBitrate = cfg.target_kbps > 0 ? cfg.target_kbps : 1000;
  codec.minBitrate = cfg.min_kbps > 0 ? cfg.min_kbps : codec.startBitrate / 2;
  codec.maxBitrate = cfg.max_kbps > 0 ? cfg.max_kbps : codec.startBitrate * 2;

  webrtc::VideoEncoder::Settings settings_webrtc(
      webrtc::VideoEncoder::Capabilities(false), 1, 1200);

  if (encoder_->InitEncode(&codec, settings_webrtc) != WEBRTC_VIDEO_CODEC_OK) {
    encoder_.reset();
    bridge_.reset();
    return HwCodecError::kHardwareFailure;
  }
  initialized_ = true;
  return HwCodecError::kOk;
}

void MppH264HwEncoder::Release() {
  if (encoder_) {
    encoder_->Release();
    encoder_.reset();
  }
  bridge_.reset();
  initialized_ = false;
}

HwCodecError MppH264HwEncoder::EncodeSync(std::shared_ptr<IVideoFrame> frame,
                                          std::shared_ptr<IEncodedPacket>* out) {
  if (!initialized_ || !encoder_ || !frame || !out) {
    return HwCodecError::kNotInitialized;
  }
  struct SyncState {
    std::mutex mu;
    std::condition_variable cv;
    std::shared_ptr<IEncodedPacket> packet;
    bool done{false};
  } state;

  class SyncCb final : public IEncoderCallback {
   public:
    explicit SyncCb(SyncState* s) : s_(s) {}
    void OnEncoded(std::shared_ptr<IEncodedPacket> packet, std::shared_ptr<IVideoFrame>) override {
      std::lock_guard<std::mutex> lk(s_->mu);
      s_->packet = std::move(packet);
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

  IEncoderCallback* prev = callback_;
  SetCallback(&sync_cb);
  const HwCodecError rc = EncodeAsync(std::move(frame));
  if (rc != HwCodecError::kOk) {
    SetCallback(prev);
    return rc;
  }
  {
    std::unique_lock<std::mutex> lk(state.mu);
    state.cv.wait_for(lk, std::chrono::seconds(2), [&] { return state.done; });
  }
  SetCallback(prev);
  if (!state.packet) {
    return HwCodecError::kTimeout;
  }
  *out = std::move(state.packet);
  return HwCodecError::kOk;
}

HwCodecError MppH264HwEncoder::EncodeAsync(std::shared_ptr<IVideoFrame> frame) {
  if (!initialized_ || !encoder_ || !frame) {
    return HwCodecError::kNotInitialized;
  }
  const webrtc::VideoFrame rtc_frame = ToWebrtcVideoFrame(*frame);
  if (!rtc_frame.video_frame_buffer()) {
    return HwCodecError::kInvalidArgument;
  }
  pending_source_ = std::move(frame);
  webrtc::VideoFrameType frame_type = webrtc::VideoFrameType::kVideoFrameDelta;
  if (force_keyframe_) {
    frame_type = webrtc::VideoFrameType::kVideoFrameKey;
    force_keyframe_ = false;
  }
  const std::vector<webrtc::VideoFrameType> types = {frame_type};
  if (encoder_->Encode(rtc_frame, &types) != WEBRTC_VIDEO_CODEC_OK) {
    pending_source_.reset();
    return HwCodecError::kHardwareFailure;
  }
  return HwCodecError::kOk;
}

void MppH264HwEncoder::SetCallback(IEncoderCallback* cb) {
  std::lock_guard<std::mutex> lk(mu_);
  callback_ = cb;
}

void MppH264HwEncoder::SetRates(uint32_t target_kbps, uint32_t min_kbps, uint32_t max_kbps) {
  if (!encoder_) {
    return;
  }
  webrtc::VideoEncoder::RateControlParameters params;
  params.bitrate = webrtc::VideoBitrateAllocation();
  params.bitrate.SetBitrate(0, 0, static_cast<int>(target_kbps * 1000));
  params.framerate_fps = config_.fps > 0 ? static_cast<double>(config_.fps) : 30.0;
  (void)min_kbps;
  (void)max_kbps;
  encoder_->SetRates(params);
}

void MppH264HwEncoder::RequestKeyframe() {
  std::lock_guard<std::mutex> lk(mu_);
  force_keyframe_ = true;
}

CodecCapabilities MppH264HwEncoder::capabilities() const { return RockchipCaps(); }

void MppH264HwEncoder::OnWebrtcEncoded(const webrtc::EncodedImage& image) {
  std::shared_ptr<IVideoFrame> source;
  IEncoderCallback* cb = nullptr;
  {
    std::lock_guard<std::mutex> lk(mu_);
    source = std::move(pending_source_);
    cb = callback_;
  }
  if (!cb) {
    return;
  }
  auto packet = EncodedPacketFromWebrtcImage(image, HwCodecId::kH264);
  if (!packet) {
    cb->OnError(HwCodecError::kHardwareFailure, "empty encoded output");
    return;
  }
  cb->OnEncoded(std::move(packet), std::move(source));
}

}  // namespace rflow::hw::rockchip

#ifndef __RFLOW_CORE_PLATFORM_ROCKCHIP_MPP_H264_HW_ENCODER_H__
#define __RFLOW_CORE_PLATFORM_ROCKCHIP_MPP_H264_HW_ENCODER_H__

#include "hw/codec/encoder.h"

#include "api/video/encoded_image.h"
#include "api/environment/environment.h"

#include <memory>
#include <mutex>

namespace rflow::rtc::hw::rockchip_mpp {
class RkMppH264Encoder;
}

namespace rflow::hw::rockchip {

class MppH264HwEncoder final : public IVideoEncoder {
 public:
  MppH264HwEncoder();
  ~MppH264HwEncoder() override;

  HwCodecError Init(const EncoderConfig& cfg) override;
  void Release() override;
  HwCodecError EncodeSync(std::shared_ptr<IVideoFrame> frame, std::shared_ptr<IEncodedPacket>* out) override;
  HwCodecError EncodeAsync(std::shared_ptr<IVideoFrame> frame) override;
  void SetCallback(IEncoderCallback* cb) override;
  void SetRates(uint32_t target_kbps, uint32_t min_kbps, uint32_t max_kbps) override;
  void RequestKeyframe() override;
  CodecCapabilities capabilities() const override;

  void OnWebrtcEncoded(const webrtc::EncodedImage& image);

 private:
  class EncodeCompleteBridge;

  webrtc::Environment env_;
  std::unique_ptr<rflow::rtc::hw::rockchip_mpp::RkMppH264Encoder> encoder_;
  std::unique_ptr<EncodeCompleteBridge> bridge_;
  IEncoderCallback* callback_{nullptr};
  EncoderConfig config_{};
  bool initialized_{false};
  bool force_keyframe_{false};
  std::shared_ptr<IVideoFrame> pending_source_;
  std::mutex mu_;
};

}  // namespace rflow::hw::rockchip

#endif  // __RFLOW_CORE_PLATFORM_ROCKCHIP_MPP_H264_HW_ENCODER_H__

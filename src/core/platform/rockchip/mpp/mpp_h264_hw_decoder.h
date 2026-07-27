#ifndef __RFLOW_CORE_PLATFORM_ROCKCHIP_MPP_H264_HW_DECODER_H__
#define __RFLOW_CORE_PLATFORM_ROCKCHIP_MPP_H264_HW_DECODER_H__

#include "hw/codec/decoder.h"

#include "api/video/video_frame.h"

#include <memory>
#include <mutex>

#include "api/environment/environment.h"

namespace rflow::rtc::hw::rockchip_mpp {
class H264Decoder;
}

namespace rflow::hw::rockchip {

class MppH264HwDecoder final : public IVideoDecoder {
 public:
  MppH264HwDecoder();
  ~MppH264HwDecoder() override;

  HwCodecError Init(const DecoderConfig& cfg) override;
  void Release() override;
  HwCodecError DecodeSync(std::shared_ptr<IEncodedPacket> packet, std::shared_ptr<IVideoFrame>* out) override;
  HwCodecError DecodeAsync(std::shared_ptr<IEncodedPacket> packet) override;
  void SetCallback(IDecoderCallback* cb) override;
  CodecCapabilities capabilities() const override;

  void OnWebrtcDecoded(const webrtc::VideoFrame& frame);

 private:
  class DecodeCompleteBridge;

  webrtc::Environment env_;
  std::unique_ptr<rflow::rtc::hw::rockchip_mpp::H264Decoder> decoder_;
  std::unique_ptr<DecodeCompleteBridge> bridge_;
  IDecoderCallback* callback_{nullptr};
  DecoderConfig config_{};
  bool initialized_{false};
  std::mutex mu_;
};

}  // namespace rflow::hw::rockchip

#endif  // __RFLOW_CORE_PLATFORM_ROCKCHIP_MPP_H264_HW_DECODER_H__

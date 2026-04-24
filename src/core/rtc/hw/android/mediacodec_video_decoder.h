#pragma once

#include <memory>

#include "api/video_codecs/video_decoder.h"

namespace rflow::rtc {

// Android NDK AMediaCodec based H.264 decoder. Decode work runs on an internal
// worker thread, while Configure/Release wait for worker-side completion.
class AndroidMediaCodecVideoDecoder : public webrtc::VideoDecoder {
 public:
    AndroidMediaCodecVideoDecoder();
    ~AndroidMediaCodecVideoDecoder() override;

    bool     Configure(const Settings& settings) override;
    int32_t  Decode(const webrtc::EncodedImage& input_image,
                    bool missing_frames,
                    int64_t render_time_ms) override;
    int32_t  RegisterDecodeCompleteCallback(webrtc::DecodedImageCallback* callback) override;
    int32_t  Release() override;
    DecoderInfo GetDecoderInfo() const override;

 private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rflow::rtc

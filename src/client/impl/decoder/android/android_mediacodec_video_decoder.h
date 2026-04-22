/**
 * @file   android_mediacodec_video_decoder.h
 * @brief  Android NDK AMediaCodec 封装的 H.264 解码器
 *
 * 从旧 webrtc_demo 命名空间迁移至 robrt::client::impl。
 * 注：Decode 工作在内部线程上执行；Configure / Release 同步等待完成。
 */

#pragma once

#include <memory>

#include "api/video_codecs/video_decoder.h"

namespace robrt::client::impl {

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

}  // namespace robrt::client::impl

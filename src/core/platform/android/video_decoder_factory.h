#pragma once

#include <memory>

#include "api/video_codecs/video_decoder_factory.h"

namespace rflow::rtc {

// Prefer NDK MediaCodec for H.264 and delegate other codecs to the builtin factory.
std::unique_ptr<webrtc::VideoDecoderFactory>
CreateAndroidHwOrBuiltinVideoDecoderFactory();

}  // namespace rflow::rtc

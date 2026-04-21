/**
 * @file   android_hw_video_decoder_factory.h
 * @brief  Android NDK MediaCodec H.264 解码器工厂（优先）+ 内置工厂回退
 *
 * 从旧 webrtc_demo 命名空间迁移至 robrt::client::impl。
 */

#pragma once

#include <memory>

#include "api/video_codecs/video_decoder_factory.h"

namespace robrt::client::impl {

// H.264 优先走 NDK MediaCodec，其余格式委托内置工厂。
std::unique_ptr<webrtc::VideoDecoderFactory>
CreateAndroidHwOrBuiltinVideoDecoderFactory();

}  // namespace robrt::client::impl

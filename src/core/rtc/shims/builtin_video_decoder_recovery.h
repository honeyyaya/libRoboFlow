/**
 * @file   builtin_video_decoder_recovery.h
 * @brief  WebRTC builtin（FFmpeg/libvpx 等）软解通用 recovery wrapper。
 *
 * 与具体编码格式无关的层：对任意 VideoDecoder 包一层 Release+Configure 恢复逻辑。
 * 各 codec 通过 RecoveryPolicyForFormat 决定启用哪些策略（如 H264 的 frame_num 回绕主动 reset）。
 */
#ifndef RFLOW_CORE_RTC_SHIMS_BUILTIN_VIDEO_DECODER_RECOVERY_H_
#define RFLOW_CORE_RTC_SHIMS_BUILTIN_VIDEO_DECODER_RECOVERY_H_

#include <memory>
#include <string>

#include "api/video_codecs/video_decoder_factory.h"

namespace rflow::rtc::shims {

struct BuiltinDecoderRecoveryPolicy {
    bool enabled = true;
    /// 连续 Decode 无 Decoded 输出超过该帧数则强制 recovery（所有 codec 通用）。
    int stall_frames_without_output = 45;
    /// >0 时：累计解码达该帧数且下一帧为 keyframe 则主动 reset（H264 frame_num 回绕等场景）。
    /// 0 表示不启用此策略。
    int proactive_reset_after_frames = 0;
};

/// 按 MIME/codec 名返回 recovery 策略（H264 含 proactive reset，其它 codec 仅 stall recovery）。
BuiltinDecoderRecoveryPolicy RecoveryPolicyForFormat(const std::string& codec_mime);

BuiltinDecoderRecoveryPolicy EvaluateBuiltinDecoderRecoveryPolicy(const std::string& codec_mime);

/// 内置解码工厂：对需 recovery 的 codec 包一层 wrapper，其余直通 WebRTC builtin。
std::unique_ptr<webrtc::VideoDecoderFactory> CreateRecoveringBuiltinVideoDecoderFactory();

}  // namespace rflow::rtc::shims

#endif  // RFLOW_CORE_RTC_SHIMS_BUILTIN_VIDEO_DECODER_RECOVERY_H_

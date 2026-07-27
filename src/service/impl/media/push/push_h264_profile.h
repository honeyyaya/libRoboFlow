#ifndef __RFLOW_SERVICE_IMPL_MEDIA_PUSH_PUSH_H264_PROFILE_H__
#define __RFLOW_SERVICE_IMPL_MEDIA_PUSH_PUSH_H264_PROFILE_H__

// Push 推流端 codec/profile 文本协议小工具（webrtc::RtpCodecCapability 维度）。
// 仅用于 service/impl/media，不做对外暴露。

#include <string>

#include "api/rtp_parameters.h"

namespace rflow::service::impl::detail::push {

// 取 capability 的 mime_type 并转小写（用于和 "video/h264" 等做匹配）。
std::string MimeLower(const webrtc::RtpCodecCapability& c);

// 把 profile-level-id 字符串规范化：转小写、去前导空白、去 0x 前缀、最多 6 位。
std::string NormalizeProfileLevelIdString(std::string v);

// 把人类配置的 H264 profile 名（main / high / baseline 等）映射成 profile_idc 前两 hex。
std::string H264ProfileIdcHex2(const std::string& profile);

// codec 是否与配置 profile 匹配；非 H264 直接 true（不影响其它 codec 选择）。
bool H264CodecMatchesConfiguredProfile(const webrtc::RtpCodecCapability& c,
                                       const std::string& want_prof_idc2);

}  // namespace rflow::service::impl::detail::push

#endif  // __RFLOW_SERVICE_IMPL_MEDIA_PUSH_PUSH_H264_PROFILE_H__

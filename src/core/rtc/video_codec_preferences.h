#ifndef __RFLOW_CORE_RTC_VIDEO_CODEC_PREFERENCES_H__
#define __RFLOW_CORE_RTC_VIDEO_CODEC_PREFERENCES_H__

#include <functional>
#include <string>
#include <vector>

#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"

namespace rflow::core::rtc {

enum class VideoCodecPreferenceRole {
    kPublisher,
    kSubscriber,
};

struct VideoCodecPreferenceConfig {
    VideoCodecPreferenceRole role = VideoCodecPreferenceRole::kPublisher;
    std::string video_codec = "h264";
    // Optional extra filter on media codecs (e.g. H264 profile_idc on publisher).
    std::function<bool(const webrtc::RtpCodecCapability&)> media_filter;
    // Subscriber: prefer baseline/constrained-baseline H264 first when true.
    bool prefer_baseline_h264_first = false;
    // When true, keep all H264 variants (not only filtered ones) to preserve RTX apt links.
    bool include_all_h264_variants = true;
    // When true (subscriber after remote offer), do not filter RTX by static apt.
    bool include_all_rtx = false;
    // When false, omit RED/ULPFEC from preferences (H264/H265: WebRTC disables them at
    // runtime with NACK anyway; use FlexFEC + RTX instead).
    bool include_ulpfec_red = true;
};

std::vector<webrtc::RtpCodecCapability> BuildVideoCodecPreferences(
    const webrtc::RtpCapabilities& caps,
    const VideoCodecPreferenceConfig& config);

bool SetVideoCodecPreferencesOnPeerConnection(
    const webrtc::scoped_refptr<webrtc::PeerConnectionInterface>& pc,
    webrtc::MediaType media_type,
    const std::vector<webrtc::RtpCodecCapability>& preferences,
    const char* log_tag);

}  // namespace rflow::core::rtc

#endif  // __RFLOW_CORE_RTC_VIDEO_CODEC_PREFERENCES_H__

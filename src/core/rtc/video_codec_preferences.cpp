#include "rtc/video_codec_preferences.h"

#include "public/logger_api.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

#include "api/video_codecs/h264_profile_level_id.h"

namespace rflow::core::rtc {
namespace {

std::string MimeLower(const webrtc::RtpCodecCapability& c) {
    std::string m = c.mime_type();
    for (char& ch : m) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return m;
}

bool MatchesPrimaryVideoCodec(const std::string& want, const webrtc::RtpCodecCapability& c) {
    const std::string m = MimeLower(c);
    if (want == "h264") {
        return m.find("h264") != std::string::npos;
    }
    if (want == "h265" || want == "hevc") {
        return m.find("h265") != std::string::npos || m.find("hevc") != std::string::npos ||
               m.find("hev1") != std::string::npos;
    }
    if (want == "vp8") {
        return m.find("vp8") != std::string::npos;
    }
    if (want == "vp9") {
        return m.find("vp9") != std::string::npos;
    }
    if (want == "av1") {
        return m.find("av1") != std::string::npos;
    }
    return m.find(want) != std::string::npos;
}

bool IsBaselineH264(const webrtc::RtpCodecCapability& codec) {
    if (codec.name != "H264") {
        return false;
    }
    const std::optional<webrtc::H264ProfileLevelId> profile =
        webrtc::ParseSdpForH264ProfileLevelId(codec.parameters);
    if (!profile.has_value()) {
        return false;
    }
    return profile->profile == webrtc::H264Profile::kProfileConstrainedBaseline ||
           profile->profile == webrtc::H264Profile::kProfileBaseline;
}

void CollectPreferredPayloadTypes(const std::vector<webrtc::RtpCodecCapability>& media,
                                  std::vector<int>* out_pts) {
    for (const auto& c : media) {
        if (c.preferred_payload_type.has_value()) {
            out_pts->push_back(*c.preferred_payload_type);
        }
    }
}

bool RtxMatchesAllowedPayloadTypes(const webrtc::RtpCodecCapability& codec,
                                   const std::vector<int>& allowed_pts) {
    const auto apt_it = codec.parameters.find("apt");
    if (apt_it == codec.parameters.end()) {
        return false;
    }
    const int apt = std::atoi(apt_it->second.c_str());
    return std::find(allowed_pts.begin(), allowed_pts.end(), apt) != allowed_pts.end();
}

void AppendResiliencyCodecs(const std::vector<webrtc::RtpCodecCapability>& auxiliary,
                            const std::vector<int>& allowed_pts,
                            bool include_all_rtx,
                            bool include_ulpfec_red,
                            std::vector<webrtc::RtpCodecCapability>* out) {
    for (const auto& codec : auxiliary) {
        if (codec.name == "rtx") {
            if (include_all_rtx || RtxMatchesAllowedPayloadTypes(codec, allowed_pts)) {
                out->push_back(codec);
            }
            continue;
        }
        if (!include_ulpfec_red && (codec.name == "red" || codec.name == "ulpfec")) {
            continue;
        }
        // flexfec-03 / ulpfec / red: resiliency codecs for weak-network recovery.
        out->push_back(codec);
    }
}

bool PrimaryCodecSupportsUlpfecWithNack(const std::string& want) {
    // WebRTC call/rtp_video_sender.cc only treats VP8/VP9 (and Generic+GenericPictureId)
    // as picture-ID-capable for NACK+ULPFEC. H264/H265 always get ULPFEC disabled at runtime.
    return want == "vp8" || want == "vp9";
}

}  // namespace

std::vector<webrtc::RtpCodecCapability> BuildVideoCodecPreferences(
    const webrtc::RtpCapabilities& caps,
    const VideoCodecPreferenceConfig& config) {
    std::string want = config.video_codec;
    for (char& ch : want) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (want.empty()) {
        want = "h264";
    }
    const bool include_ulpfec_red =
        config.include_ulpfec_red && PrimaryCodecSupportsUlpfecWithNack(want);

    std::vector<webrtc::RtpCodecCapability> preferred_media;
    std::vector<webrtc::RtpCodecCapability> other_h264_media;
    std::vector<webrtc::RtpCodecCapability> other_media;
    std::vector<webrtc::RtpCodecCapability> auxiliary;

    for (const auto& codec : caps.codecs) {
        if (codec.kind != webrtc::MediaType::VIDEO) {
            continue;
        }
        if (!codec.IsMediaCodec()) {
            auxiliary.push_back(codec);
            continue;
        }
        if (!MatchesPrimaryVideoCodec(want, codec)) {
            other_media.push_back(codec);
            continue;
        }

        if (want == "h264" && config.include_all_h264_variants && codec.name == "H264") {
            const bool passes_filter = !config.media_filter || config.media_filter(codec);
            if (passes_filter) {
                if (config.role == VideoCodecPreferenceRole::kSubscriber &&
                    config.prefer_baseline_h264_first && IsBaselineH264(codec)) {
                    preferred_media.push_back(codec);
                } else if (config.role == VideoCodecPreferenceRole::kSubscriber &&
                           config.prefer_baseline_h264_first) {
                    other_h264_media.push_back(codec);
                } else {
                    preferred_media.push_back(codec);
                }
            } else {
                other_h264_media.push_back(codec);
            }
            continue;
        }

        if (config.media_filter && !config.media_filter(codec)) {
            continue;
        }
        preferred_media.push_back(codec);
    }

    if (preferred_media.empty() && other_h264_media.empty()) {
        return {};
    }

    std::vector<webrtc::RtpCodecCapability> media_ordered = preferred_media;
    media_ordered.insert(media_ordered.end(), other_h264_media.begin(), other_h264_media.end());
    media_ordered.insert(media_ordered.end(), other_media.begin(), other_media.end());

    std::vector<int> allowed_pts;
    CollectPreferredPayloadTypes(media_ordered, &allowed_pts);

    std::vector<webrtc::RtpCodecCapability> result = std::move(media_ordered);
    AppendResiliencyCodecs(auxiliary, allowed_pts, config.include_all_rtx, include_ulpfec_red,
                           &result);
    return result;
}

bool SetVideoCodecPreferencesOnPeerConnection(
    const webrtc::scoped_refptr<webrtc::PeerConnectionInterface>& pc,
    webrtc::MediaType media_type,
    const std::vector<webrtc::RtpCodecCapability>& preferences,
    const char* log_tag) {
    if (!pc || preferences.empty()) {
        return false;
    }
    std::vector<webrtc::RtpCodecCapability> prefs = preferences;
    for (const auto& tr : pc->GetTransceivers()) {
        if (!tr || tr->media_type() != media_type) {
            continue;
        }
        const webrtc::RTCError err = tr->SetCodecPreferences(
            webrtc::ArrayView<webrtc::RtpCodecCapability>(prefs.data(), prefs.size()));
        if (!err.ok()) {
            RFLOW_LOGE("[%s] SetCodecPreferences failed: %s", log_tag, err.message());
            return false;
        }
        RFLOW_LOGI("[%s] SetCodecPreferences ok, codecs=%zu", log_tag, preferences.size());
        return true;
    }
    RFLOW_LOGE("[%s] SetCodecPreferences: no matching transceiver", log_tag);
    return false;
}

}  // namespace rflow::core::rtc

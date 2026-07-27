#include "media/push/push_h264_profile.h"

#include <cctype>

namespace rflow::service::impl::detail::push {

std::string MimeLower(const webrtc::RtpCodecCapability& c) {
    std::string m = c.mime_type();
    for (auto& ch : m) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return m;
}

std::string NormalizeProfileLevelIdString(std::string v) {
    for (auto& ch : v) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    size_t i = 0;
    while (i < v.size() && (v[i] == ' ' || v[i] == '\t')) {
        ++i;
    }
    v = v.substr(i);
    if (v.size() >= 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
        v = v.substr(2);
    }
    if (v.size() > 6) {
        v.resize(6);
    }
    return v;
}

std::string H264ProfileIdcHex2(const std::string& profile) {
    std::string p = profile;
    for (auto& c : p) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (p == "main" || p == "m") {
        return "4d";
    }
    if (p == "high" || p == "h") {
        return "64";
    }
    if (p == "baseline" || p == "base" || p == "b") {
        return "42";
    }
    return "4d";
}

bool H264CodecMatchesConfiguredProfile(const webrtc::RtpCodecCapability& c,
                                       const std::string& want_prof_idc2) {
    if (c.name != "H264") {
        return true;
    }
    auto it = c.parameters.find("profile-level-id");
    if (it == c.parameters.end()) {
        return true;
    }
    std::string cap = NormalizeProfileLevelIdString(it->second);
    if (cap.size() < 2) {
        return true;
    }
    return cap.substr(0, 2) == want_prof_idc2;
}

}  // namespace rflow::service::impl::detail::push

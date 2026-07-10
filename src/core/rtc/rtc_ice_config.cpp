#include "rtc/rtc_ice_config.h"

#include "runtime/runtime_knobs.h"

#include <cctype>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "api/environment/environment_factory.h"
#include "api/peer_connection_interface.h"
#include "rtc_base/network.h"
#include "rtc_base/thread.h"

namespace rflow::rtc {
namespace {

namespace knob = rflow::core::runtime;

std::mutex g_ice_ignore_mu;
std::optional<std::vector<std::string>> g_ice_ignore_explicit;

std::string TrimAscii(std::string s) {
    const auto begin = std::find_if_not(s.begin(), s.end(),
                                        [](unsigned char c) { return std::isspace(c) != 0; });
    const auto rend =
        std::find_if_not(s.rbegin(), s.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (begin >= rend) {
        return {};
    }
    return std::string(begin, rend);
}

bool IsPlausibleInterfaceName(const std::string& name) {
    if (name.empty() || name.size() > 32u) {
        return false;
    }
    for (unsigned char c : name) {
        if (std::isalnum(c) != 0 || c == '_' || c == '-' || c == '.') {
            continue;
        }
        return false;
    }
    return true;
}

std::vector<std::string> ParseCsvInternal(const char* csv) {
    std::vector<std::string> out;
    if (!csv) {
        return out;
    }
    std::string input(csv);
    std::stringstream ss(input);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = TrimAscii(std::move(token));
        if (token.empty() || !IsPlausibleInterfaceName(token)) {
            continue;
        }
        out.push_back(std::move(token));
    }
    return out;
}

std::vector<std::string> ReadEnvIceIgnoreList() {
    const std::string csv = knob::ReadString("RFLOW_ICE_IGNORE_INTERFACES");
    return ParseCsvInternal(csv.empty() ? nullptr : csv.c_str());
}

void AppendIceServer(const std::string& url,
                     const std::string& username,
                     const std::string& password,
                     webrtc::PeerConnectionInterface::IceServers* servers) {
    if (url.empty()) {
        return;
    }
    webrtc::PeerConnectionInterface::IceServer ice;
    ice.urls.push_back(url);
    if (!username.empty()) {
        ice.username = username;
    }
    if (!password.empty()) {
        ice.password = password;
    }
    servers->push_back(std::move(ice));
}

}  // namespace

const char* DefaultStunServerUrl() {
    return "stun:stun.l.google.com:19302";
}

std::vector<std::string> ParseCommaSeparatedInterfaceList(const char* csv) {
    return ParseCsvInternal(csv);
}

void NotifyIceIgnoreInterfacesFromSdkConfig(const char* comma_separated_or_null) {
    std::lock_guard<std::mutex> lk(g_ice_ignore_mu);
    if (!comma_separated_or_null) {
        g_ice_ignore_explicit.reset();
        return;
    }
    g_ice_ignore_explicit = ParseCsvInternal(comma_separated_or_null);
}

void ResetIceIgnoreInterfacesSdkOverride() {
    std::lock_guard<std::mutex> lk(g_ice_ignore_mu);
    g_ice_ignore_explicit.reset();
}

std::vector<std::string> IceIgnoreInterfacesEffective() {
    {
        std::lock_guard<std::mutex> lk(g_ice_ignore_mu);
        if (g_ice_ignore_explicit.has_value()) {
            return *g_ice_ignore_explicit;
        }
    }
    return ReadEnvIceIgnoreList();
}

webrtc::PeerConnectionInterface::RTCConfiguration BuildRtcConfiguration(
    const IceRtcServerConfig& ice,
    bool disable_link_local_networks,
    bool disable_ipv6_on_wifi) {
    webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;
    const std::string stun_url =
        ice.has_stun_server ? ice.stun_server : std::string(DefaultStunServerUrl());
    AppendIceServer(stun_url, {}, {}, &rtc_config.servers);
    if (ice.has_turn_server && !ice.turn_server.empty()) {
        AppendIceServer(ice.turn_server, ice.turn_username, ice.turn_password, &rtc_config.servers);
    }
    rtc_config.disable_ipv6_on_wifi = disable_ipv6_on_wifi;
    rtc_config.max_ipv6_networks = 0;
    rtc_config.disable_link_local_networks = disable_link_local_networks;
    rtc_config.bundle_policy = webrtc::PeerConnectionInterface::kBundlePolicyMaxBundle;
    rtc_config.tcp_candidate_policy = webrtc::PeerConnectionInterface::kTcpCandidatePolicyDisabled;
    rtc_config.set_dscp(true);
    rtc_config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    rtc_config.prioritize_most_likely_ice_candidate_pairs = ice.ice_prioritize_likely_pairs;
    return rtc_config;
}

void MaybeInjectFilteredNetworkManager(webrtc::PeerConnectionFactoryDependencies& deps) {
    if (deps.network_manager) {
        return;
    }
    const std::vector<std::string> ignore = IceIgnoreInterfacesEffective();
    if (ignore.empty() || !deps.network_thread) {
        return;
    }
    webrtc::SocketServer* ss = deps.network_thread->socketserver();
    if (!ss) {
        return;
    }
    webrtc::Environment env = webrtc::CreateEnvironment();
    auto network_manager = std::make_unique<webrtc::BasicNetworkManager>(env, ss);
    network_manager->set_network_ignore_list(ignore);
    deps.network_manager = std::move(network_manager);
}

}  // namespace rflow::rtc

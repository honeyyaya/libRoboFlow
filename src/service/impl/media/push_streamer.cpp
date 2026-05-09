#include "media/push_streamer.h"

#include "api/array_view.h"
#include "camera/camera_utils.h"
#include "media/camera_video_track_source.h"
#include "media/external_push_video_track_source.h"
#include "common/base/env_reader.h"
#include "common/base/trace_switches.h"
#include "common/media/frame_types.h"
#include "common/public/log_tagged.h"
#include "core/rtc/peer_connection_factory_deps.h"
#include "core/rtc/sdp_observers.h"
#include "core/rtc/stats_observer.h"
#include "core/runtime/runtime_knobs.h"
#include "core/thread/thread_pool.h"
#include "media/push_h264_profile.h"
#include "media/push_streamer_internals.h"
#include "media/push_streamer_observers.h"

#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/priority.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/set_local_description_observer_interface.h"
#include "api/set_remote_description_observer_interface.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "modules/video_capture/video_capture_factory.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/thread.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <unistd.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rflow::service::impl {

using detail::push::ClosePeerConnectionWithDeadline;
using detail::push::LatencyTraceEnabled;
using detail::push::MakeOfferOptions;
using detail::push::MakeRtcConfiguration;
using detail::push::ParseVideoNetworkPriority;
using detail::push::PrintOutboundVideoStats;
using detail::push::SignalingNowUs;
using detail::push::SignalingTimingTraceEnabled;
using detail::push::StopWebrtcThreadWithDeadline;
using detail::push::TraceSigTiming;

using observers::DecodedFrameSink;
using observers::FrameCountingSink;
using observers::LoopbackPcObserver;

using detail::push::H264CodecMatchesConfiguredProfile;
using detail::push::H264ProfileIdcHex2;
using detail::push::MimeLower;
using detail::push::NormalizeProfileLevelIdString;

class PushStreamer::Impl : public webrtc::PeerConnectionObserver {
public:
    explicit Impl(const PushStreamerConfig& config) : config_(config) {}

    class ExtraPeerObserver : public webrtc::PeerConnectionObserver {
    public:
        ExtraPeerObserver(Impl* owner, std::string peer_id) : owner_(owner), peer_id_(std::move(peer_id)) {}

        void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
        void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
        void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override {}
        void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override {
            if (owner_ && candidate) {
                owner_->OnPeerIceCandidate(peer_id_, candidate);
            }
        }
        void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState s) override {
            if (owner_) {
                owner_->NotifyConnectionState(s);
            }
        }

    private:
        Impl* owner_;
        std::string peer_id_;
    };

    bool Initialize() {
        RFLOW_LOG_TAG_I("PushStreamer", "Initializing WebRTC (native API)...");
        rflow::rtc::EnsureWebrtcFieldTrialsInitialized();
        if (!webrtc::InitializeSSL()) {
            RFLOW_LOG_TAG_E("PushStreamer", "InitializeSSL failed");
            return false;
        }

        webrtc::PeerConnectionFactoryDependencies deps;
        rflow::rtc::PeerConnectionFactoryMediaOptions media_opts;
        media_opts.encoder_backend = config_.backend.use_rockchip_mpp_h264
                                         ? rflow::rtc::VideoCodecBackendPreference::kRockchipMpp
                                         : rflow::rtc::VideoCodecBackendPreference::kBuiltin;
        rflow::rtc::ConfigurePeerConnectionFactoryDependencies(deps, &media_opts);
        rflow::rtc::EnsureDedicatedPeerConnectionSignalingThread(deps, &owned_signaling_thread_);

        factory_ = webrtc::CreateModularPeerConnectionFactory(std::move(deps));
        if (!factory_) {
            RFLOW_LOG_TAG_E("PushStreamer", "CreateModularPeerConnectionFactory failed");
            return false;
        }
        RFLOW_LOG_TAG_I("PushStreamer", "PeerConnectionFactory created");

        return CreatePeerConnection();
    }

    void Shutdown() {
        StopOutboundStatsLoop();
        if (frame_counter_ && camera_source_) {
            camera_source_->RemoveSink(frame_counter_.get());
        }
        frame_counter_.reset();
        camera_impl_ = nullptr;

        // 回环：先关 receiver（易阻塞，带超时），再关 sender 与多路 PC（与 pull_subscriber 一致，均在 Shutdown 调用线程上 Close）。
        if (loopback_observer_) {
            loopback_observer_->Teardown();
        }
        if (receiver_) {
            webrtc::scoped_refptr<webrtc::PeerConnectionInterface> recv = receiver_;
            receiver_ = nullptr;
            ClosePeerConnectionWithDeadline(recv, "loopback receiver", 8);
        }
        loopback_observer_.reset();

        if (peer_connection_) {
            webrtc::scoped_refptr<webrtc::PeerConnectionInterface> sender = peer_connection_;
            peer_connection_ = nullptr;
            ClosePeerConnectionWithDeadline(sender, "publisher", 8);
        }
        for (auto& kv : peer_connections_) {
            if (kv.second) {
                ClosePeerConnectionWithDeadline(kv.second, "subscriber", 8);
            }
        }
        peer_connections_.clear();
        extra_peer_observers_.clear();

        video_track_ = nullptr;
        camera_source_ = nullptr;
        camera_impl_ = nullptr;
        external_source_ = nullptr;

        factory_ = nullptr;

        if (owned_signaling_thread_) {
            if (!owned_signaling_thread_->IsCurrent()) {
                StopWebrtcThreadWithDeadline(owned_signaling_thread_.get(), 6);
            }
            owned_signaling_thread_.reset();
        }

        webrtc::CleanupSSL();
    }

    bool CreatePeerConnection() {
        auto rtc_config = MakeRtcConfiguration(config_);
        auto pc = CreatePcWithObserver(this, rtc_config);
        if (!pc) {
            return false;
        }
        peer_connection_ = pc;

        if (!CreateMediaTracks()) {
            return false;
        }
        if (!video_track_) {
            RFLOW_LOG_TAG_E("PushStreamer", "No video track");
            return false;
        }

        std::vector<std::string> stream_ids = {config_.common.stream_id};
        // 多订阅者 + 仅对订阅者发 Offer：同一 VideoTrack 不要同时挂到「占位」默认 PC 与订阅者 PC，
        // 否则部分 libwebrtc 版本在第二路 CreateOffer 上可能长期不回调（拉流端收不到 SDP）。
        if (!config_.common.signaling_subscriber_offer_only) {
            auto add = peer_connection_->AddTrack(video_track_, stream_ids);
            if (!add.ok()) {
                RFLOW_LOG_TAG_E("PushStreamer", "AddTrack failed: %s", add.error().message());
                return false;
            }
            ApplyVideoCodecPreferences(peer_connection_);
            ApplyEncodingParameters(peer_connection_);
            RFLOW_LOG_TAG_I("PushStreamer", "Video track added (stream_id=%s)",
                            config_.common.stream_id.c_str());
            MaybeStartOutboundStatsLoop();
        } else {
            std::cout << "[PushStreamer] Video track ready (subscriber-offer-only; sender per subscriber PC, "
                         "stream_id="
                      << config_.common.stream_id << ")" << std::endl;
        }
        return true;
    }

    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> CreatePcWithObserver(
        webrtc::PeerConnectionObserver* observer,
        const webrtc::PeerConnectionInterface::RTCConfiguration& cfg) {
        webrtc::PeerConnectionDependencies deps(observer);
        auto result = factory_->CreatePeerConnectionOrError(cfg, std::move(deps));
        if (!result.ok()) {
            RFLOW_LOG_TAG_E("PushStreamer", "CreatePeerConnection failed: %s", result.error().message());
            return nullptr;
        }
        return result.MoveValue();
    }

    void ApplyVideoCodecPreferences(webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc) {
        if (!factory_ || !pc) {
            return;
        }
        std::string want = config_.common.video_codec;
        for (auto& ch : want) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (want.empty()) {
            want = "h264";
        }

        webrtc::RtpCapabilities caps = factory_->GetRtpSenderCapabilities(webrtc::MediaType::VIDEO);
        if (caps.codecs.empty()) {
            std::cerr << "[PushStreamer] GetRtpSenderCapabilities(VIDEO) empty" << std::endl;
            return;
        }

        auto match_want = [&](const std::string& m) -> bool {
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
        };

        std::vector<webrtc::RtpCodecCapability> preferred;
        std::vector<webrtc::RtpCodecCapability> other;
        for (const auto& c : caps.codecs) {
            if (match_want(MimeLower(c))) {
                preferred.push_back(c);
            } else {
                other.push_back(c);
            }
        }
        if (preferred.empty()) {
            std::cout << "[PushStreamer] SetCodecPreferences skipped: no match for VIDEO_CODEC=" << config_.common.video_codec
                      << std::endl;
            return;
        }
        if (want == "h264") {
            const std::string want_idc = H264ProfileIdcHex2(config_.common.h264_profile);
            std::vector<webrtc::RtpCodecCapability> filtered;
            filtered.reserve(preferred.size());
            for (const auto& c : preferred) {
                if (H264CodecMatchesConfiguredProfile(c, want_idc)) {
                    filtered.push_back(c);
                }
            }
            if (!filtered.empty()) {
                preferred = std::move(filtered);
                std::cout << "[PushStreamer] H264 profile filter: profile_idc=0x" << want_idc << " (H264_PROFILE="
                          << config_.common.h264_profile << ")" << std::endl;
            } else {
                std::cout << "[PushStreamer] H264 profile filter skipped: no payload matched profile_idc=0x" << want_idc
                          << ", using all H264 payloads" << std::endl;
            }
        }
        std::vector<webrtc::RtpCodecCapability> ordered;
        ordered.reserve(preferred.size() + other.size());
        ordered.insert(ordered.end(), preferred.begin(), preferred.end());
        ordered.insert(ordered.end(), other.begin(), other.end());

        for (auto tr : pc->GetTransceivers()) {
            if (!tr || tr->media_type() != webrtc::MediaType::VIDEO) {
                continue;
            }
            auto err = tr->SetCodecPreferences(webrtc::ArrayView<webrtc::RtpCodecCapability>(
                ordered.data(), ordered.size()));
            if (!err.ok()) {
                std::cerr << "[PushStreamer] SetCodecPreferences: " << err.message() << std::endl;
            } else {
                std::cout << "[PushStreamer] SetCodecPreferences: prefer " << want << std::endl;
            }
            return;
        }
    }

    void ApplyEncodingParameters(webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc) {
        for (auto tr : pc->GetTransceivers()) {
            if (!tr || tr->media_type() != webrtc::MediaType::VIDEO) {
                continue;
            }
            auto sender = tr->sender();
            if (!sender) {
                continue;
            }
            webrtc::RtpParameters params = sender->GetParameters();
            const webrtc::Priority net_prio = ParseVideoNetworkPriority(config_.common.video_network_priority);
            const int fps_cap = (config_.common.video_encoding_max_framerate > 0) ? config_.common.video_encoding_max_framerate
                                                                           : config_.common.video_fps;
            const double max_fps = (fps_cap > 0) ? static_cast<double>(fps_cap) : 30.0;
            for (auto& enc : params.encodings) {
                enc.min_bitrate_bps = config_.common.min_bitrate_kbps * 1000;
                enc.max_bitrate_bps = config_.common.max_bitrate_kbps * 1000;
                enc.network_priority = net_prio;
                enc.max_framerate = max_fps;
            }
            if (config_.common.degradation_preference == "maintain_resolution") {
                params.degradation_preference = webrtc::DegradationPreference::MAINTAIN_RESOLUTION;
            } else if (config_.common.degradation_preference == "maintain_framerate") {
                params.degradation_preference = webrtc::DegradationPreference::MAINTAIN_FRAMERATE;
            } else if (config_.common.degradation_preference == "balanced") {
                params.degradation_preference = webrtc::DegradationPreference::BALANCED;
            } else {
                params.degradation_preference = webrtc::DegradationPreference::MAINTAIN_FRAMERATE;
            }
            auto err = sender->SetParameters(params);
            if (!err.ok()) {
                std::cerr << "[PushStreamer] SetParameters failed: " << err.message() << std::endl;
            } else {
                webrtc::BitrateSettings br;
                const int min_bps = std::max(0, config_.common.min_bitrate_kbps * 1000);
                const int target_bps = std::max(min_bps, config_.common.target_bitrate_kbps * 1000);
                const int max_bps = std::max(target_bps, config_.common.max_bitrate_kbps * 1000);
                br.min_bitrate_bps = min_bps;
                br.start_bitrate_bps = target_bps;
                br.max_bitrate_bps = max_bps;
                auto br_err = pc->SetBitrate(br);
                if (!br_err.ok()) {
                    std::cerr << "[PushStreamer] SetBitrate failed: " << br_err.message() << std::endl;
                } else {
                    std::cout << "[PushStreamer] SetBitrate: min/start/max=" << min_bps << "/" << target_bps
                              << "/" << max_bps << " bps" << std::endl;
                }
                std::cout << "[PushStreamer] Encoding params: bitrate " << config_.common.min_bitrate_kbps << "-"
                          << config_.common.max_bitrate_kbps << " kbps"
                          << " max_fps=" << max_fps
                          << " network_priority=" << config_.common.video_network_priority << std::endl;
                {
                    const char* deg = "maintain_framerate";
                    if (config_.common.degradation_preference == "maintain_resolution") {
                        deg = "maintain_resolution";
                    } else if (config_.common.degradation_preference == "balanced") {
                        deg = "balanced";
                    }
                    std::cout << "[PushStreamer] degradation_preference=" << deg
                              << " (maintain_framerate: 弱网时优先保帧、倾向降分辨率)\n";
                }
                if (LatencyTraceEnabled()) {
                    std::cout << "[Latency] WebRTC degradation_preference trace ok\n";
                }
            }
            break;
        }
    }

    void MaybeStartOutboundStatsLoop() {
        const int interval_sec =
            rflow::core::runtime::ReadInt("WEBRTC_PUSH_OUTBOUND_STATS_INTERVAL_SEC");
        if (interval_sec <= 0 || outbound_stats_started_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        outbound_stats_stop_.store(false, std::memory_order_release);
        outbound_stats_interval_sec_.store(interval_sec, std::memory_order_relaxed);
        std::cout << "[PushStreamer] Outbound stats enabled, interval=" << interval_sec
                  << "s (process thread_pool scheduled)" << std::endl;
        // 通过 thread_pool::post_after 实现自重投，无需常驻线程；shutdown 时
        // outbound_stats_stop_ 置位即可让下一次 tick 退出。
        ScheduleOutboundStatsTick();
    }

    void ScheduleOutboundStatsTick() {
        if (outbound_stats_stop_.load(std::memory_order_acquire)) {
            return;
        }
        const int interval_sec = outbound_stats_interval_sec_.load(std::memory_order_relaxed);
        rflow::thread::post([this, interval_sec]() {
            if (outbound_stats_stop_.load(std::memory_order_acquire)) {
                return;
            }
            auto [pc, tag] = SelectStatsPeerConnection();
            if (pc) {
                auto cb = rflow::core::rtc::MakeStatsCollectorObserver(
                    [tag](const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
                        PrintOutboundVideoStats(tag, report);
                    });
                pc->GetStats(cb.get());
            }
            if (outbound_stats_stop_.load(std::memory_order_acquire)) {
                return;
            }
            rflow::thread::post_after(std::chrono::seconds(interval_sec),
                                      [this]() { ScheduleOutboundStatsTick(); });
        });
    }

    void StopOutboundStatsLoop() {
        outbound_stats_stop_.store(true, std::memory_order_release);
        // 已 in-flight 的 task 会在执行入口判 outbound_stats_stop_ 自然退出；
        // post_after 中的延时任务在 shutdown_infrastructure → thread_pool::shutdown 时
        // 会被一并丢弃。无需 join。
        outbound_stats_started_.store(false, std::memory_order_release);
    }

    std::pair<webrtc::scoped_refptr<webrtc::PeerConnectionInterface>, std::string> SelectStatsPeerConnection() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& kv : peer_connections_) {
            if (kv.second) {
                return {kv.second, kv.first};
            }
        }
        if (peer_connection_) {
            return {peer_connection_, "default"};
        }
        return {nullptr, "none"};
    }

    bool ResolveDeviceUniqueId(std::string* out_unique) {
        // 显式 /dev/videoN 时优先按路径直采，勿依赖 WebRTC 枚举（枚举偶发为 0 时仍应能推流）。
        // 是否 CAPTURE 由 CameraVideoTrackSource::StartDirectV4l2 再验；此处不要求枚举下标算成功。
        if (!config_.common.video_device_path.empty()) {
            const std::string& p = config_.common.video_device_path;
            if (p.rfind("/dev/video", 0) == 0 && access(p.c_str(), R_OK | W_OK) == 0) {
                *out_unique = p;
                std::cout << "[PushStreamer] Camera " << p << " -> capture by device path (multi-node safe)"
                          << std::endl;
                return true;
            }
        }

        std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(webrtc::VideoCaptureFactory::CreateDeviceInfo());
        if (!info) {
            std::cerr << "[PushStreamer] CreateDeviceInfo failed" << std::endl;
            return false;
        }
        uint32_t n = info->NumberOfDevices();
        if (n == 0) {
            std::cerr << "[PushStreamer] No V4L2/video capture devices" << std::endl;
            return false;
        }

        if (!config_.common.video_device_path.empty()) {
            // 与 device_info_v4l2 一致：/dev/videoN → 第 N' 个 CAPTURE 节点的枚举下标（非路径配置等）
            {
                int path_idx = GetWebRtcCaptureDeviceIndexForPath(config_.common.video_device_path);
                if (path_idx >= 0 && static_cast<uint32_t>(path_idx) < n) {
                    char name[256] = {0};
                    char unique[256] = {0};
                    char product[256] = {0};
                    if (info->GetDeviceName(static_cast<uint32_t>(path_idx), name, sizeof(name), unique, sizeof(unique),
                                            product, sizeof(product)) == 0) {
                        *out_unique = unique;
                        std::cout << "[PushStreamer] Camera path " << config_.common.video_device_path
                                  << " -> /dev/video enum index match" << std::endl;
                        return true;
                    }
                }
            }
            // Linux 上 GetDeviceName 的 unique 实为 V4L2 bus_info（product 常为空，勿用 product==bus）
            std::string bus = GetDeviceBusInfo(config_.common.video_device_path);
            for (uint32_t i = 0; i < n; ++i) {
                char name[256] = {0};
                char unique[256] = {0};
                char product[256] = {0};
                if (info->GetDeviceName(i, name, sizeof(name), unique, sizeof(unique), product, sizeof(product)) != 0) {
                    continue;
                }
                if (!bus.empty() && std::string(unique) == bus) {
                    *out_unique = unique;
                    std::cout << "[PushStreamer] Camera path " << config_.common.video_device_path << " -> bus_info match"
                              << std::endl;
                    return true;
                }
            }
            for (uint32_t i = 0; i < n; ++i) {
                char name[256] = {0};
                char unique[256] = {0};
                char product[256] = {0};
                if (info->GetDeviceName(i, name, sizeof(name), unique, sizeof(unique), product, sizeof(product)) != 0) {
                    continue;
                }
                if (std::string(unique).find(config_.common.video_device_path) != std::string::npos) {
                    *out_unique = unique;
                    return true;
                }
            }
            // WebRTC 的 unique 往往不含 /dev/videoN；用 V4L2 card 与枚举设备名对齐（常见）
            {
                std::string card = GetDeviceCardName(config_.common.video_device_path);
                if (!card.empty()) {
                    for (uint32_t i = 0; i < n; ++i) {
                        char name[256] = {0};
                        char unique[256] = {0};
                        char product[256] = {0};
                        if (info->GetDeviceName(i, name, sizeof(name), unique, sizeof(unique), product, sizeof(product)) !=
                            0) {
                            continue;
                        }
                        if (std::string(name) == card) {
                            *out_unique = unique;
                            std::cout << "[PushStreamer] Camera path " << config_.common.video_device_path
                                      << " -> device name/card match" << std::endl;
                            return true;
                        }
                    }
                }
            }
            std::cerr << "[PushStreamer] No device match for " << config_.common.video_device_path << ", using index 0"
                      << std::endl;
        }

        uint32_t idx = static_cast<uint32_t>(config_.common.video_device_index);
        if (idx >= n) {
            std::cerr << "[PushStreamer] Device index out of range" << std::endl;
            return false;
        }
        char name[256] = {0};
        char unique[256] = {0};
        if (info->GetDeviceName(idx, name, sizeof(name), unique, sizeof(unique)) != 0) {
            return false;
        }
        *out_unique = unique;
        return true;
    }

    bool CreateMediaTracks() {
        if (config_.common.use_external_video_source) {
            return CreateMediaTracksExternal();
        }
        std::string unique_id;
        if (!ResolveDeviceUniqueId(&unique_id)) {
            return false;
        }

        auto* cam_holder = new webrtc::RefCountedObject<CameraVideoTrackSource>();
        camera_impl_ = static_cast<CameraVideoTrackSource*>(cam_holder);
        camera_source_ = webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface>(
            static_cast<webrtc::VideoTrackSourceInterface*>(camera_impl_));

        // 须在 Start()/采集线程起来之前注册 sink，否则早期帧进 broadcaster 时 sink 列表仍为空。
        if (!frame_counter_) {
            frame_counter_ = std::make_unique<FrameCountingSink>(on_frame_);
            camera_source_->AddOrUpdateSink(frame_counter_.get(), webrtc::VideoSinkWants());
        }

        bool mpp_mjpeg_decode = config_.backend.use_rockchip_mpp_mjpeg_decode;
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
        // 默认值取决于 backend cfg；env 显式覆盖（命名空间走 runtime_knobs，alias 兼容老命名）。
        const bool allow_dual_mpp =
            (std::getenv("WEBRTC_DUAL_MPP_MJPEG_H264") != nullptr)
                ? rflow::core::runtime::ReadBool("WEBRTC_DUAL_MPP_MJPEG_H264")
                : config_.backend.use_rockchip_dual_mpp_mjpeg_h264;
        if (mpp_mjpeg_decode && config_.backend.use_rockchip_mpp_h264 && !allow_dual_mpp) {
            mpp_mjpeg_decode = false;
            std::cout << "[PushStreamer] MPP MJPEG decode off while MPP H.264 encode on (use libyuv for MJPEG). "
                         "Set USE_DUAL_MPP_MJPEG_H264=1 or WEBRTC_DUAL_MPP_MJPEG_H264=1 to enable both.\n";
        } else if (mpp_mjpeg_decode && config_.backend.use_rockchip_mpp_h264 && allow_dual_mpp) {
            std::cout << "[PushStreamer] Dual MPP: MJPEG hardware decode + H.264 hardware encode (experimental).\n";
        }
#endif
        V4l2MjpegPipelineOptions mjpeg_pipe;
        mjpeg_pipe.mjpeg_queue_latest_only = config_.backend.mjpeg_queue_latest_only;
        mjpeg_pipe.mjpeg_queue_max = config_.backend.mjpeg_queue_max;
        mjpeg_pipe.nv12_pool_slots = config_.backend.nv12_pool_slots;
        mjpeg_pipe.v4l2_buffer_count = config_.backend.v4l2_buffer_count;
        mjpeg_pipe.v4l2_poll_timeout_ms = config_.backend.v4l2_poll_timeout_ms;
        mjpeg_pipe.mjpeg_decode_inline = config_.backend.mjpeg_decode_inline;
        mjpeg_pipe.mjpeg_v4l2_ext_dma = config_.backend.mjpeg_v4l2_ext_dma;
        mjpeg_pipe.mjpeg_rga_to_mpp = config_.backend.mjpeg_rga_to_mpp;
        if (!static_cast<CameraVideoTrackSource*>(cam_holder)
                 ->Start(unique_id.c_str(), config_.common.video_width, config_.common.video_height, config_.common.video_fps,
                         mpp_mjpeg_decode, &mjpeg_pipe)) {
            RFLOW_LOG_TAG_E("PushStreamer", "CameraVideoTrackSource::Start failed");
            if (frame_counter_ && camera_source_) {
                camera_source_->RemoveSink(frame_counter_.get());
            }
            frame_counter_.reset();
            camera_source_ = nullptr;
            camera_impl_ = nullptr;
            return false;
        }

        video_track_ = factory_->CreateVideoTrack(camera_source_, "video_track");
        if (!video_track_) {
            RFLOW_LOG_TAG_E("PushStreamer", "CreateVideoTrack failed");
            return false;
        }
        // 提示发送链按「运动/实时」内容处理，利于码控与帧类型决策（见 VideoTrackInterface::ContentHint）。
        video_track_->set_content_hint(webrtc::VideoTrackInterface::ContentHint::kFluid);

        RFLOW_LOG_TAG_I("PushStreamer", "Video capture started");

        {
            int cam_fps = 0;
            if (camera_impl_->GetNegotiatedCaptureFramerate(&cam_fps) && cam_fps > 0) {
                if (cam_fps != config_.common.video_fps) {
                    std::cout << "[PushStreamer] Using camera actual frame rate " << cam_fps << " fps (config FPS="
                              << config_.common.video_fps << " was request only; encoding/WebRTC caps follow device)\n";
                }
                config_.common.video_fps = cam_fps;
            }
        }

        {
            int nw = 0;
            int nh = 0;
            if (camera_impl_->GetNegotiatedCaptureSize(&nw, &nh) &&
                (nw != config_.common.video_width || nh != config_.common.video_height)) {
                std::cout << "[PushStreamer] V4L2 negotiated " << nw << "x" << nh << ", config requests "
                          << config_.common.video_width << "x" << config_.common.video_height
                          << " — set WIDTH/HEIGHT in streams.conf to match to reduce capture/encode scaling.\n";
            }
        }

        if (config_.common.capture_warmup_sec > 0) {
            std::cout << "[PushStreamer] Camera warmup " << config_.common.capture_warmup_sec << "s..." << std::endl;
            const auto w0 = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::seconds(config_.common.capture_warmup_sec));
            if (LatencyTraceEnabled()) {
                const auto wms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - w0)
                                     .count();
                std::cout << "[Latency] capture_warmup actual_ms=" << wms << " configured_sec=" << config_.common.capture_warmup_sec
                          << std::endl;
            }
        }
        return true;
    }

    bool CreateMediaTracksExternal() {
        auto* ext_holder = new webrtc::RefCountedObject<ExternalPushVideoTrackSource>();
        external_source_ = webrtc::scoped_refptr<ExternalPushVideoTrackSource>(
            static_cast<ExternalPushVideoTrackSource*>(ext_holder));
        camera_source_ = webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface>(
            static_cast<webrtc::VideoTrackSourceInterface*>(ext_holder));

        if (!frame_counter_) {
            frame_counter_ = std::make_unique<FrameCountingSink>(on_frame_);
            camera_source_->AddOrUpdateSink(frame_counter_.get(), webrtc::VideoSinkWants());
        }

        video_track_ = factory_->CreateVideoTrack(camera_source_, "video_track");
        if (!video_track_) {
            RFLOW_LOG_TAG_E("PushStreamer", "CreateVideoTrack (external source) failed");
            if (frame_counter_ && camera_source_) {
                camera_source_->RemoveSink(frame_counter_.get());
            }
            frame_counter_.reset();
            camera_source_ = nullptr;
            external_source_ = nullptr;
            return false;
        }
        video_track_->set_content_hint(webrtc::VideoTrackInterface::ContentHint::kFluid);

        std::cout << "[PushStreamer] External video source ready "
                  << config_.common.video_width << "x" << config_.common.video_height
                  << " fps=" << config_.common.video_fps << std::endl;
        return true;
    }

    webrtc::scoped_refptr<ExternalPushVideoTrackSource> external_source() const {
        return external_source_;
    }

    bool WaitForCaptureGate(const std::string& context) {
        const int need = config_.common.capture_gate_min_frames;
        if (need <= 0 || !frame_counter_) {
            return true;
        }
        int max_wait = config_.common.capture_gate_max_wait_sec;
        if (max_wait < 1) {
            max_wait = 1;
        }
        using clock = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::seconds(max_wait);
        const auto gate_t0 = clock::now();
        std::cout << "[PushStreamer] Capture gate: need >= " << need << " frames (" << context << "), max wait "
                  << max_wait << "s" << std::endl;
        while (clock::now() < deadline) {
            const unsigned int got = EffectiveCaptureFrameCount();
            if (got >= static_cast<unsigned int>(need)) {
                std::cout << "[PushStreamer] Capture gate OK: " << got << " frames" << std::endl;
                if (LatencyTraceEnabled()) {
                    const auto gms =
                        std::chrono::duration<double, std::milli>(clock::now() - gate_t0).count();
                    std::cout << "[Latency] capture_gate elapsed_ms=" << gms << " frames=" << got << " need=" << need
                              << std::endl;
                }
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        std::cerr << "[PushStreamer] Capture gate failed (" << context << ")" << std::endl;
        if (LatencyTraceEnabled()) {
            const auto gms = std::chrono::duration<double, std::milli>(clock::now() - gate_t0).count();
            std::cout << "[Latency] capture_gate TIMEOUT elapsed_ms=" << gms << " last_frames=" << EffectiveCaptureFrameCount()
                      << " need=" << need << std::endl;
        }
        return false;
    }

    void CreateOffer() {
        if (!WaitForCaptureGate("default")) {
            return;
        }
        if (!peer_connection_) {
            return;
        }
        webrtc::Thread* sig = peer_connection_->signaling_thread();
        if (!sig) {
            std::cerr << "[PushStreamer] CreateOffer: no signaling thread" << std::endl;
            return;
        }
        std::cout << "[PushStreamer] Creating default Offer..." << std::endl;
        auto run = [this]() { CreateOfferOnConnection("", peer_connection_); };
        if (sig->IsCurrent()) {
            run();
        } else {
            sig->BlockingCall(run);
        }
    }

    void CreateOfferForPeer(const std::string& peer_id) {
        if (peer_id.empty()) {
            CreateOffer();
            return;
        }
        if (!peer_connection_) {
            std::cerr << "[PushStreamer] CreateOfferForPeer: no default peer connection" << std::endl;
            return;
        }
        if (!WaitForCaptureGate(std::string("peer=") + peer_id)) {
            return;
        }
        TraceSigTiming("CreateOfferForPeer gate_ok peer=" + peer_id);
        webrtc::Thread* sig = peer_connection_->signaling_thread();
        if (!sig) {
            std::cerr << "[PushStreamer] CreateOfferForPeer: no signaling thread" << std::endl;
            return;
        }
        // AddTrack / CreateOffer 必须在专用 signaling 线程（见 EnsureDedicatedPeerConnectionSignalingThread）。
        auto run = [this, peer_id]() {
            if (!EnsurePeerConnectionForPeer(peer_id)) {
                return;
            }
            webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = peer_connections_.find(peer_id);
                if (it != peer_connections_.end()) {
                    pc = it->second;
                }
            }
            if (!pc) {
                std::cerr << "[PushStreamer] peer not found: " << peer_id << std::endl;
                return;
            }
            std::cout << "[PushStreamer] Creating Offer for subscriber: " << peer_id << std::endl;
            CreateOfferOnConnection(peer_id, pc);
        };
        if (sig->IsCurrent()) {
            run();
        } else {
            sig->BlockingCall(run);
        }
    }

    bool EnsurePeerConnectionForPeer(const std::string& peer_id) {
        if (peer_id.empty()) {
            return peer_connection_ != nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (peer_connections_.find(peer_id) != peer_connections_.end()) {
                return true;
            }
        }
        // 禁止在持 mutex_ 期间 CreatePeerConnection/AddTrack：WebRTC 可能同步触发观察者回调，
        // 与主线程里其它持 mutex_ 的逻辑抢锁会死锁 → 不发 Offer、拉流永远 0 帧。
        auto observer = std::make_unique<ExtraPeerObserver>(this, peer_id);
        auto pc = CreatePcWithObserver(observer.get(), MakeRtcConfiguration(config_));
        if (!pc) {
            return false;
        }
        if (!video_track_) {
            RFLOW_LOG_TAG_E("PushStreamer", "EnsurePeerConnectionForPeer: no video track");
            return false;
        }
        std::vector<std::string> stream_ids = {config_.common.stream_id};
        auto add = pc->AddTrack(video_track_, stream_ids);
        if (!add.ok()) {
            RFLOW_LOG_TAG_E("PushStreamer", "AddTrack for peer failed: %s", add.error().message());
            return false;
        }
        ApplyVideoCodecPreferences(pc);
        ApplyEncodingParameters(pc);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (peer_connections_.find(peer_id) != peer_connections_.end()) {
                return true;
            }
            peer_connections_[peer_id] = pc;
            extra_peer_observers_[peer_id] = std::move(observer);
        }
        RFLOW_LOG_TAG_I("PushStreamer", "Subscriber PeerConnection created: %s", peer_id.c_str());
        MaybeStartOutboundStatsLoop();
        return true;
    }

    void CreateOfferOnConnection(const std::string& peer_id,
                                 webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc) {
        if (!pc) {
            return;
        }
        auto opts = MakeOfferOptions();
        TraceSigTiming("CreateOfferOnConnection begin peer=" + (peer_id.empty() ? std::string("default") : peer_id));
        auto peer_id_ptr = std::make_shared<std::string>(peer_id);

        auto obs = rflow::core::rtc::MakeCreateSdpObserver(
                [this, peer_id_ptr, pc](std::unique_ptr<webrtc::SessionDescriptionInterface> desc) {
                    if (!desc) {
                        return;
                    }
                    std::string type = desc->type();
                    std::string sdp;
                    if (!desc->ToString(&sdp)) {
                        std::cerr << "[PushStreamer] SDP ToString failed" << std::endl;
                        return;
                    }
                    TraceSigTiming("CreateOffer success peer=" +
                                   (peer_id_ptr->empty() ? std::string("default") : *peer_id_ptr) +
                                   " sdp_len=" + std::to_string(sdp.size()));

                    auto set_local = rflow::core::rtc::MakeSetLocalDescObserver(
                            [this, peer_id_ptr, type, sdp](webrtc::RTCError err) {
                                if (!err.ok()) {
                                    std::cerr << "[PushStreamer] SetLocalDescription failed: " << err.message()
                                              << std::endl;
                                    return;
                                }
                                TraceSigTiming("SetLocalDescription OK peer=" +
                                               (peer_id_ptr->empty() ? std::string("default") : *peer_id_ptr));
                                const bool dump_offer = rflow::core::runtime::ReadBool("WEBRTC_DUMP_OFFER");
                                if (config_.common.test_encode_mode && peer_id_ptr->empty()) {
                                    if (dump_offer) {
                                        std::cout << "\n--- Local offer SDP ---\n" << sdp << "\n--- End ---\n" << std::flush;
                                    }
                                    DoLoopbackExchange(type, sdp);
                                } else {
                                    if (dump_offer && on_sdp_) {
                                        std::cout << "\n--- Local offer SDP (peer=" << *peer_id_ptr << ") ---\n" << sdp
                                                  << "\n--- End ---\n" << std::flush;
                                    }
                                    if (on_sdp_) {
                                        on_sdp_(*peer_id_ptr, type, sdp);
                                    }
                                }
                            });

                    pc->SetLocalDescription(std::move(desc), set_local);
                },
                [](webrtc::RTCError err) {
                    RFLOW_LOG_TAG_E("PushStreamer", "CreateOffer failed: %s", err.message());
                });

        pc->CreateOffer(obs.get(), opts);
    }

    void SetRemoteDescription(const std::string& type, const std::string& sdp) {
        SetRemoteDescriptionForPeer("", type, sdp);
    }

    void SetRemoteDescriptionForPeer(const std::string& peer_id, const std::string& type, const std::string& sdp) {
        webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc = peer_connection_;
        if (!peer_id.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = peer_connections_.find(peer_id);
            if (it != peer_connections_.end()) {
                pc = it->second;
            }
        }
        if (!pc) {
            std::cerr << "[PushStreamer] SetRemoteDescription: no peer" << std::endl;
            return;
        }
        webrtc::Thread* sig = pc->signaling_thread();
        if (!sig) {
            std::cerr << "[PushStreamer] SetRemoteDescription: no signaling thread" << std::endl;
            return;
        }
        auto work = [pc, type, sdp]() {
            TraceSigTiming("SetRemoteDescription begin type=" + type + " sdp_len=" + std::to_string(sdp.size()));
            auto opt_type = webrtc::SdpTypeFromString(type);
            if (!opt_type.has_value()) {
                std::cerr << "[PushStreamer] Bad SDP type: " << type << std::endl;
                return;
            }
            auto desc = webrtc::CreateSessionDescription(*opt_type, sdp);
            if (!desc) {
                std::cerr << "[PushStreamer] CreateSessionDescription(parse) failed" << std::endl;
                return;
            }
            auto obs = rflow::core::rtc::MakeSetRemoteDescObserver([](webrtc::RTCError err) {
                if (!err.ok()) {
                    RFLOW_LOG_TAG_E("PushStreamer", "SetRemoteDescription failed: %s", err.message());
                } else {
                    TraceSigTiming("SetRemoteDescription OK");
                    RFLOW_LOG_TAG_I("PushStreamer", "SetRemoteDescription OK");
                }
            });
            pc->SetRemoteDescription(std::move(desc), obs);
        };
        if (sig->IsCurrent()) {
            work();
        } else {
            sig->BlockingCall(work);
        }
    }

    void AddRemoteIceCandidate(const std::string& mid, int mline_index, const std::string& candidate) {
        AddRemoteIceCandidateForPeer("", mid, mline_index, candidate);
    }

    void AddRemoteIceCandidateForPeer(const std::string& peer_id, const std::string& mid, int mline_index,
                                      const std::string& candidate) {
        webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc = peer_connection_;
        if (!peer_id.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = peer_connections_.find(peer_id);
            if (it != peer_connections_.end()) {
                pc = it->second;
            }
        }
        if (!pc) {
            return;
        }
        webrtc::Thread* sig = pc->signaling_thread();
        if (!sig) {
            return;
        }
        auto work = [pc, mid, mline_index, candidate]() {
            TraceSigTiming("AddRemoteIce begin mid=" + mid + " cand_len=" + std::to_string(candidate.size()));
            webrtc::SdpParseError err;
            webrtc::IceCandidateInterface* cand = webrtc::CreateIceCandidate(mid, mline_index, candidate, &err);
            if (!cand) {
                std::cerr << "[PushStreamer] CreateIceCandidate failed: " << err.description << std::endl;
                return;
            }
            std::unique_ptr<webrtc::IceCandidateInterface> owned(cand);
            if (!pc->AddIceCandidate(owned.get())) {
                std::cerr << "[PushStreamer] AddIceCandidate failed" << std::endl;
            }
        };
        if (sig->IsCurrent()) {
            work();
        } else {
            sig->BlockingCall(work);
        }
    }

    void DoLoopbackExchange(const std::string& offer_type, const std::string& offer_sdp) {
        if (rflow::core::runtime::ReadBool("WEBRTC_SKIP_LOOPBACK_RECV")) {
            std::cout << "[PushStreamer] Loopback skipped (WEBRTC_SKIP_LOOPBACK_RECV=1)\n";
            return;
        }
        std::cout << "[PushStreamer] Loopback: creating receiver PC..." << std::endl;
        auto rtc_config = MakeRtcConfiguration(config_);
        loopback_observer_ = std::make_unique<LoopbackPcObserver>(
            [this](const std::string& mid, int idx, const std::string& cand) {
                webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc = peer_connection_;
                if (!pc) {
                    return;
                }
                webrtc::Thread* sig = pc->signaling_thread();
                if (!sig) {
                    return;
                }
                auto work = [pc, mid, idx, cand]() {
                    webrtc::SdpParseError err;
                    auto* ic = webrtc::CreateIceCandidate(mid, idx, cand, &err);
                    if (!ic) {
                        return;
                    }
                    std::unique_ptr<webrtc::IceCandidateInterface> o(ic);
                    pc->AddIceCandidate(o.get());
                };
                if (sig->IsCurrent()) {
                    work();
                } else {
                    sig->BlockingCall(work);
                }
            });
        receiver_ = CreatePcWithObserver(loopback_observer_.get(), rtc_config);
        if (!receiver_) {
            return;
        }

        webrtc::RtpTransceiverInit init;
        init.direction = webrtc::RtpTransceiverDirection::kRecvOnly;
        auto tr = receiver_->AddTransceiver(webrtc::MediaType::VIDEO, init);
        if (!tr.ok()) {
            std::cerr << "[PushStreamer] AddTransceiver failed" << std::endl;
            return;
        }

        auto opt_offer = webrtc::SdpTypeFromString(offer_type);
        if (!opt_offer.has_value()) {
            return;
        }
        auto remote_offer = webrtc::CreateSessionDescription(*opt_offer, offer_sdp);
        if (!remote_offer) {
            return;
        }

        auto obs_remote = rflow::core::rtc::MakeSetRemoteDescObserver(
                [this](webrtc::RTCError e) {
                    if (!e.ok()) {
                        std::cerr << "[PushStreamer] receiver SetRemote failed: " << e.message() << std::endl;
                        return;
                    }
                    OnReceiverRemoteDescriptionSet();
                });
        receiver_->SetRemoteDescription(std::move(remote_offer), obs_remote);
    }

    void OnReceiverRemoteDescriptionSet() {
        auto opts = MakeOfferOptions();
        auto obs = rflow::core::rtc::MakeCreateSdpObserver(
                [this](std::unique_ptr<webrtc::SessionDescriptionInterface> desc) {
                    if (!desc) {
                        return;
                    }
                    loopback_answer_sdp_.clear();
                    loopback_answer_type_ = desc->type();
                    if (!desc->ToString(&loopback_answer_sdp_)) {
                        return;
                    }
                    auto set_local = rflow::core::rtc::MakeSetLocalDescObserver(
                            [this](webrtc::RTCError err) {
                                if (!err.ok()) {
                                    std::cerr << "[PushStreamer] receiver SetLocal failed: " << err.message()
                                              << std::endl;
                                    return;
                                }
                                OnReceiverLocalDescriptionSet();
                            });
                    receiver_->SetLocalDescription(std::move(desc), set_local);
                },
                [](webrtc::RTCError err) {
                    std::cerr << "[PushStreamer] CreateAnswer failed: " << err.message() << std::endl;
                });
        receiver_->CreateAnswer(obs.get(), opts);
    }

    void OnReceiverLocalDescriptionSet() {
        auto opt_t = webrtc::SdpTypeFromString(loopback_answer_type_);
        if (!opt_t.has_value()) {
            return;
        }
        auto answer = webrtc::CreateSessionDescription(*opt_t, loopback_answer_sdp_);
        if (!answer) {
            return;
        }
        auto obs = rflow::core::rtc::MakeSetRemoteDescObserver([](webrtc::RTCError e) {
            if (e.ok()) {
                std::cout << "[PushStreamer] Loopback SDP exchange done" << std::endl;
            }
        });
        peer_connection_->SetRemoteDescription(std::move(answer), obs);
    }

    void NotifyConnectionState(webrtc::PeerConnectionInterface::PeerConnectionState state) {
        if (!on_connection_state_) {
            return;
        }
        ConnectionState cs = ConnectionState::New;
        switch (state) {
            case webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting:
                cs = ConnectionState::Connecting;
                break;
            case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:
                cs = ConnectionState::Connected;
                break;
            case webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected:
                cs = ConnectionState::Disconnected;
                break;
            case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed:
                cs = ConnectionState::Failed;
                break;
            case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:
                cs = ConnectionState::Closed;
                break;
            default:
                break;
        }
        on_connection_state_(cs);
    }

    void OnPeerIceCandidate(const std::string& peer_id, const webrtc::IceCandidateInterface* candidate) {
        if (!candidate || !on_ice_candidate_) {
            return;
        }
        std::string sdp;
        if (!candidate->ToString(&sdp)) {
            return;
        }
        on_ice_candidate_(peer_id, candidate->sdp_mid(), candidate->sdp_mline_index(), sdp);
    }

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState state) override {
        const char* names[] = {"New", "Gathering", "Complete"};
        int idx = static_cast<int>(state);
        if (idx >= 0 && idx < 3) {
            std::cout << "[PushStreamer] ICE gathering: " << names[idx] << std::endl;
        }
    }

    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override {
        if (!candidate) {
            return;
        }
        std::string sdp;
        if (!candidate->ToString(&sdp)) {
            return;
        }
        if (config_.common.test_encode_mode && receiver_) {
            webrtc::scoped_refptr<webrtc::PeerConnectionInterface> recv = receiver_;
            webrtc::Thread* sig = recv->signaling_thread();
            if (!sig) {
                return;
            }
            const std::string mid = candidate->sdp_mid();
            const int mline_index = candidate->sdp_mline_index();
            auto work = [recv, mid, mline_index, sdp]() {
                webrtc::SdpParseError err;
                auto* ic = webrtc::CreateIceCandidate(mid, mline_index, sdp, &err);
                if (!ic) {
                    return;
                }
                std::unique_ptr<webrtc::IceCandidateInterface> o(ic);
                recv->AddIceCandidate(o.get());
            };
            if (sig->IsCurrent()) {
                work();
            } else {
                sig->BlockingCall(work);
            }
        } else if (on_ice_candidate_) {
            on_ice_candidate_("", candidate->sdp_mid(), candidate->sdp_mline_index(), sdp);
        }
    }

    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState new_state) override {
        NotifyConnectionState(new_state);
    }

    void SetOnSdp(OnSdpCallback cb) { on_sdp_ = std::move(cb); }
    void SetOnIceCandidate(OnIceCandidateCallback cb) { on_ice_candidate_ = std::move(cb); }
    void SetOnConnectionState(OnConnectionStateCallback cb) { on_connection_state_ = std::move(cb); }
    void SetOnFrame(OnFrameCallback cb) {
        on_frame_ = std::move(cb);
        if (camera_source_ && frame_counter_) {
            camera_source_->RemoveSink(frame_counter_.get());
        }
        frame_counter_ = std::make_unique<FrameCountingSink>(on_frame_);
        if (camera_source_) {
            camera_source_->AddOrUpdateSink(frame_counter_.get(), webrtc::VideoSinkWants());
        }
    }

    unsigned int EffectiveCaptureFrameCount() const {
        unsigned int n = frame_counter_ ? frame_counter_->GetFrameCount() : 0;
        if (camera_impl_) {
            n = std::max(n, static_cast<unsigned int>(camera_impl_->CapturedFrameCount()));
        }
        return n;
    }

    unsigned int GetFrameCount() const {
        return EffectiveCaptureFrameCount();
    }
    unsigned int GetDecodedFrameCount() const {
        return loopback_observer_ ? loopback_observer_->GetDecodedCount() : 0;
    }
    bool TestCaptureOnly() const { return config_.common.test_capture_only; }
    bool SignalingSubscriberOfferOnly() const { return config_.common.signaling_subscriber_offer_only; }

private:
    PushStreamerConfig config_;
    std::unique_ptr<webrtc::Thread> owned_signaling_thread_;
    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
    webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> camera_source_;
    /// 与 camera_source_ 同生命周期的具体采集实现（避免 dynamic_cast/RTTI 依赖）。
    CameraVideoTrackSource* camera_impl_{nullptr};
    /// 业务侧 push 模式下，camera_source_ 实际指向此对象；保留强引用用于 push 调用。
    webrtc::scoped_refptr<ExternalPushVideoTrackSource> external_source_;

    OnSdpCallback on_sdp_;
    OnIceCandidateCallback on_ice_candidate_;
    OnConnectionStateCallback on_connection_state_;
    OnFrameCallback on_frame_;
    std::unique_ptr<FrameCountingSink> frame_counter_;

    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> receiver_;
    std::unique_ptr<LoopbackPcObserver> loopback_observer_;
    std::string loopback_answer_sdp_;
    std::string loopback_answer_type_;

    std::unordered_map<std::string, webrtc::scoped_refptr<webrtc::PeerConnectionInterface>> peer_connections_;
    std::unordered_map<std::string, std::unique_ptr<ExtraPeerObserver>> extra_peer_observers_;
    std::mutex mutex_;
    std::atomic<bool> outbound_stats_stop_{false};
    std::atomic<bool> outbound_stats_started_{false};
    std::atomic<int>  outbound_stats_interval_sec_{1};
};

PushStreamer::PushStreamer(const PushStreamerConfig& config) : impl_(std::make_unique<Impl>(config)) {}

PushStreamer::~PushStreamer() {
    Stop();
}

bool PushStreamer::Start() {
    if (is_streaming_.load(std::memory_order_acquire)) {
        return true;
    }
    if (!impl_->Initialize()) {
        return false;
    }
    if (!impl_->TestCaptureOnly() && !impl_->SignalingSubscriberOfferOnly()) {
        impl_->CreateOffer();
    }
    is_streaming_.store(true, std::memory_order_release);
    return true;
}

void PushStreamer::Stop() {
    // exchange：SIGTERM 等信号在 Shutdown 阻塞时重入 Stop 时不得第二次 Shutdown。
    if (!is_streaming_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    impl_->Shutdown();
}

bool PushStreamer::SetRemoteDescription(const std::string& type, const std::string& sdp) {
    impl_->SetRemoteDescription(type, sdp);
    return true;
}

bool PushStreamer::SetRemoteDescriptionForPeer(const std::string& peer_id, const std::string& type,
                                               const std::string& sdp) {
    impl_->SetRemoteDescriptionForPeer(peer_id, type, sdp);
    return true;
}

void PushStreamer::AddRemoteIceCandidate(const std::string& mid, int mline_index, const std::string& candidate) {
    impl_->AddRemoteIceCandidate(mid, mline_index, candidate);
}

void PushStreamer::AddRemoteIceCandidateForPeer(const std::string& peer_id, const std::string& mid, int mline_index,
                                                const std::string& candidate) {
    impl_->AddRemoteIceCandidateForPeer(peer_id, mid, mline_index, candidate);
}

void PushStreamer::CreateOfferForPeer(const std::string& peer_id) {
    impl_->CreateOfferForPeer(peer_id);
}

void PushStreamer::SetOnSdpCallback(OnSdpCallback cb) {
    impl_->SetOnSdp(std::move(cb));
}

void PushStreamer::SetOnIceCandidateCallback(OnIceCandidateCallback cb) {
    impl_->SetOnIceCandidate(std::move(cb));
}

void PushStreamer::SetOnConnectionStateCallback(OnConnectionStateCallback cb) {
    impl_->SetOnConnectionState(std::move(cb));
}

void PushStreamer::SetOnFrameCallback(OnFrameCallback cb) {
    impl_->SetOnFrame(std::move(cb));
}

bool PushStreamer::PushExternalI420(const uint8_t* data_y, int stride_y,
                                     const uint8_t* data_u, int stride_u,
                                     const uint8_t* data_v, int stride_v,
                                     int width, int height, int64_t timestamp_us) {
    auto src = impl_->external_source();
    if (!src) return false;
    return src->PushI420(data_y, stride_y, data_u, stride_u, data_v, stride_v,
                         width, height, timestamp_us);
}

bool PushStreamer::PushExternalI420Contiguous(const uint8_t* buf, uint32_t size,
                                              int width, int height, int64_t timestamp_us) {
    auto src = impl_->external_source();
    if (!src) return false;
    return src->PushI420Contiguous(buf, size, width, height, timestamp_us);
}

bool PushStreamer::PushExternalNv12(const uint8_t* data_y, int stride_y,
                                     const uint8_t* data_uv, int stride_uv,
                                     int width, int height, int64_t timestamp_us) {
    auto src = impl_->external_source();
    if (!src) return false;
    return src->PushNv12(data_y, stride_y, data_uv, stride_uv, width, height, timestamp_us);
}

bool PushStreamer::PushExternalNv12Contiguous(const uint8_t* buf, uint32_t size,
                                              int width, int height, int64_t timestamp_us) {
    auto src = impl_->external_source();
    if (!src) return false;
    return src->PushNv12Contiguous(buf, size, width, height, timestamp_us);
}

unsigned int PushStreamer::GetFrameCount() const {
    return impl_->GetFrameCount();
}

unsigned int PushStreamer::GetDecodedFrameCount() const {
    return impl_->GetDecodedFrameCount();
}

bool PushStreamer::CollectStats(librflow_stream_stats_s* out_stats) {
    if (!out_stats) return false;

    auto [pc, tag] = impl_->SelectStatsPeerConnection();
    if (!pc) return false;

    struct Snapshot {
        bool done = false;
        uint64_t out_bytes = 0;
        uint64_t out_pkts = 0;
        uint32_t lost_pkts = 0;
        uint32_t fps = 0;
        uint32_t bitrate_kbps = 0;
        uint32_t rtt_ms = 0;
    };

    std::mutex mu;
    std::condition_variable cv;
    Snapshot snapshot;

    auto callback = rflow::core::rtc::MakeStatsCollectorObserver(
        [&mu, &cv, &snapshot](const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
            Snapshot local;
            if (report) {
                for (const auto* outbound : report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>()) {
                    if (!outbound || !outbound->kind || *outbound->kind != "video") continue;
                    if (outbound->bytes_sent) local.out_bytes += *outbound->bytes_sent;
                    if (outbound->packets_sent) local.out_pkts += *outbound->packets_sent;
                    if (outbound->frames_per_second) {
                        local.fps = std::max(local.fps,
                                             static_cast<uint32_t>(*outbound->frames_per_second + 0.5));
                    }
                }
                for (const auto* remote_in : report->GetStatsOfType<webrtc::RTCRemoteInboundRtpStreamStats>()) {
                    if (!remote_in || !remote_in->kind || *remote_in->kind != "video") continue;
                    if (remote_in->packets_lost && *remote_in->packets_lost > 0) {
                        local.lost_pkts += static_cast<uint32_t>(*remote_in->packets_lost);
                    }
                    if (remote_in->round_trip_time) {
                        local.rtt_ms = std::max(
                            local.rtt_ms,
                            static_cast<uint32_t>(*remote_in->round_trip_time * 1000.0 + 0.5));
                    }
                }
                for (const auto* pair : report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>()) {
                    if (!pair) continue;
                    if (pair->current_round_trip_time) {
                        local.rtt_ms = std::max(
                            local.rtt_ms,
                            static_cast<uint32_t>(*pair->current_round_trip_time * 1000.0 + 0.5));
                    }
                    if (pair->available_outgoing_bitrate) {
                        local.bitrate_kbps = std::max(
                            local.bitrate_kbps,
                            static_cast<uint32_t>(*pair->available_outgoing_bitrate / 1000.0 + 0.5));
                    }
                }
            }
            {
                std::lock_guard<std::mutex> lk(mu);
                snapshot = local;
                snapshot.done = true;
            }
            cv.notify_one();
        });

    pc->GetStats(callback.get());

    std::unique_lock<std::mutex> lk(mu);
    if (!cv.wait_for(lk, std::chrono::milliseconds(1500), [&snapshot] { return snapshot.done; })) {
        return false;
    }

    out_stats->out_bound_bytes = snapshot.out_bytes;
    out_stats->out_bound_pkts = snapshot.out_pkts;
    out_stats->lost_pkts = snapshot.lost_pkts;
    if (snapshot.fps > 0) out_stats->fps = snapshot.fps;
    out_stats->bitrate_kbps = snapshot.bitrate_kbps;
    out_stats->rtt_ms = snapshot.rtt_ms;
    return true;
}

}  // namespace rflow::service::impl

/**
 * @file   push_demo_sdk.cpp
 * @brief  基于 librflow_svc C ABI 的推流 demo
 *
 * 用法:
 *   ./push_demo_sdk <signaling_url> [device_id] [width] [height] [fps] [stream_idx] [camera]
 *
 * 逻辑拆分：
 *   - common/demo_helpers.*       信号、日志、相机路径
 *   - common/demo_camera_hotplug.*  Linux USB 相机 udev/探测
 *   - common/demo_push_session.*    信令连接、推流生命周期与调度
 *
 * RTP FlexFEC、degradation、stream_param 等见 demo_push_session.cpp / 文件头历史说明。
 */

#include "rflow/Service/librflow_service_api.h"

#include "common/demo_camera_hotplug.h"
#include "common/demo_helpers.h"
#include "common/demo_privilege.h"
#include "common/demo_push_session.h"

#include <chrono>
#include <iostream>

int main(int argc, char* argv[]) {
    rflow::apps::common::DropRootPrivilegesIfSudoInvoked();
    rflow::apps::common::PushDemoConfig cfg;
    if (!rflow::apps::common::ParsePushDemoConfig(argc, argv, cfg)) {
        return 1;
    }

    rflow::apps::common::InstallStopSignals();
    cfg.camera = rflow::apps::common::PickLinuxCameraPath(cfg.camera);
#if defined(__linux__)
    rflow::apps::common::WarnIfCameraPathNotReady(cfg.camera);
#endif

    rflow::apps::common::PushDemoSession session(cfg);

    auto sig_cfg = librflow_signal_config_create();
    librflow_signal_config_set_url(sig_cfg, cfg.signaling_url.c_str());
    auto gcfg = librflow_global_config_create();
    librflow_global_config_set_signal(gcfg, sig_cfg);
    librflow_global_config_set_flexfec(gcfg, RFLOW_GLOBAL_FLEXFEC_ON);
    // 双网卡时忽略非推流网卡，避免 ICE 在 eth0(2.x) 上 STUN/候选干扰；单网卡可保持注释。
    // librflow_global_config_set_ice_ignore_interfaces(gcfg, "eth0");

    if (librflow_svc_set_global_config(gcfg) != RFLOW_OK) {
        std::cerr << "svc_set_global_config failed\n";
        return 1;
    }
    librflow_global_config_destroy(gcfg);
    librflow_signal_config_destroy(sig_cfg);

    if (librflow_svc_init() != RFLOW_OK) {
        std::cerr << "svc_init failed\n";
        return 1;
    }

    if (!session.Connect()) {
        librflow_svc_uninit();
        return 1;
    }

#if defined(__linux__)
    session.camera().SyncInitialPresence();
    session.camera().Start();
    session.camera().PollWait(std::chrono::steady_clock::now(), false);
    if (session.camera().RestartRequested()) {
        session.set_want_stream_restart(true);
        session.camera().ClearRestartRequest();
    }
#endif

    if (!session.TryStartStream()) {
        std::cout << "[demo] initial stream deferred; retrying in main loop" << std::endl;
    }

    std::cout << "[demo] global_config FlexFEC=ON (forced via librflow_global_config_set_flexfec)" << std::endl;
    std::cout << "[demo] stream_idx=" << cfg.stream_idx << " room=" << cfg.device_id << ":" << cfg.stream_idx
              << std::endl;
    std::cout << "[demo] degradation_preference=MAINTAIN_FRAMERATE" << std::endl;
    std::cout << "[demo] SDK internal capture enabled, target " << cfg.width << "x" << cfg.height << "@"
              << cfg.fps << " to " << cfg.signaling_url;
#if defined(__linux__)
    std::cout << " camera=" << session.config().camera;
    if (session.camera().UsesUdev()) {
        std::cout << " (hotplug: libudev video4linux monitor)";
    }
#endif
    std::cout << std::endl;
    std::cout << "[demo] Ctrl+C to stop." << std::endl;

    auto last_stats = std::chrono::steady_clock::now();
    while (!rflow::apps::common::StopRequested()) {
        const auto now = std::chrono::steady_clock::now();

#if defined(__linux__)
        session.camera().PollWait(now, session.stream() != nullptr);
#endif

        session.ServiceTick(now);

        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_stats).count() >= 5) {
            last_stats = now;
            if (!session.stream()) continue;
            librflow_stream_stats_t stats = nullptr;
            if (librflow_svc_stream_get_stats(session.stream(), &stats) == RFLOW_OK && stats) {
                std::cout << "[demo][stats]"
                          << " duration_ms=" << librflow_stream_stats_get_duration_ms(stats)
                          << " out_bytes=" << librflow_stream_stats_get_out_bound_bytes(stats)
                          << " out_pkts=" << librflow_stream_stats_get_out_bound_pkts(stats)
                          << " fps=" << librflow_stream_stats_get_fps(stats)
                          << " kbps=" << librflow_stream_stats_get_bitrate_kbps(stats)
                          << " rtt=" << librflow_stream_stats_get_rtt_ms(stats)
                          << " lost=" << librflow_stream_stats_get_lost_pkts(stats)
                          << std::endl;
                librflow_stream_stats_release(stats);
            }
        }
    }

    std::cout << "[demo] stopping..." << std::endl;
    session.set_shutting_down(true);
#if defined(__linux__)
    session.camera().Stop();
#endif
    session.StopStreamForExit();
    librflow_svc_disconnect();
    librflow_svc_uninit();
    return session.exit_due_to_failures() ? 2 : 0;
}

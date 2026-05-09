/**
 * @file   push_demo_sdk.cpp
 * @brief  基于 librflow_svc C ABI 的推流 demo
 *
 * 用法:
 *   ./push_demo_sdk <signaling_url> [device_id] [width] [height] [fps] [stream_idx] [camera]
 *
 * 默认:
 *   signaling_url = 127.0.0.1:8765
 *   device_id     = demo_device
 *   分辨率/帧率   = 640x360 @ 30
 *   stream_idx    = 0
 *   camera        = Linux 下优先 RFLOW_PUSH_DEMO_CAMERA，其次 /dev/video0；其他平台默认索引 0
 *
 * 说明:
 *   本 demo 只调用 SDK，采集/编码/推流全部由 SDK 内部完成。
 */

#include "rflow/Service/librflow_service_api.h"

#include "common/demo_helpers.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

void OnConnectState(rflow_connect_state_t state, rflow_err_t reason, void* /*ud*/) {
    rflow::apps::common::LogConnectState(state, reason);
}

void OnBindState(rflow_bind_state_t state, const char* /*detail*/, void* /*ud*/) {
    std::cout << "[demo] bind state=" << state << std::endl;
}

void OnPullRequest(rflow_stream_index_t idx, void* /*ud*/) {
    std::cout << "[demo] on_pull_request idx=" << idx << std::endl;
}

void OnPullRelease(rflow_stream_index_t idx, void* /*ud*/) {
    std::cout << "[demo] on_pull_release idx=" << idx << std::endl;
}

void OnStreamState(librflow_svc_stream_handle_t /*h*/, rflow_stream_state_t state,
                   rflow_err_t reason, void* /*ud*/) {
    rflow::apps::common::LogStreamState(state, reason);
}

}  // namespace

int main(int argc, char** argv) {
    std::string signaling_url = "127.0.0.1:8765";
    std::string device_id     = "demo_device";
    int width                 = 640;
    int height                = 360;
    int fps                   = 30;
    rflow_stream_index_t stream_idx = 0;
    std::string camera;

    if (argc >= 2) signaling_url = argv[1];
    if (argc >= 3) device_id = argv[2];
    if (argc >= 4) width = std::atoi(argv[3]);
    if (argc >= 5) height = std::atoi(argv[4]);
    if (argc >= 6) fps = std::atoi(argv[5]);
    if (argc >= 7) stream_idx = static_cast<rflow_stream_index_t>(std::atoi(argv[6]));
    if (argc >= 8) camera = argv[7];

    rflow::apps::common::InstallStopSignals();
    camera = rflow::apps::common::PickLinuxCameraPath(camera);

    auto sig_cfg = librflow_signal_config_create();
    librflow_signal_config_set_url(sig_cfg, signaling_url.c_str());
    auto gcfg = librflow_global_config_create();
    librflow_global_config_set_signal(gcfg, sig_cfg);

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

    auto info = librflow_svc_connect_info_create();
    librflow_svc_connect_info_set_device_id(info, device_id.c_str());
    librflow_svc_connect_info_set_device_secret(info, "");
    librflow_svc_connect_info_set_product_key(info, "rflow_demo");
    librflow_svc_connect_info_set_vendor_id(info, "rflow");

    auto ccb = librflow_svc_connect_cb_create();
    librflow_svc_connect_cb_set_on_state(ccb, OnConnectState);
    librflow_svc_connect_cb_set_on_bind_state(ccb, OnBindState);
    librflow_svc_connect_cb_set_on_pull_request(ccb, OnPullRequest);
    librflow_svc_connect_cb_set_on_pull_release(ccb, OnPullRelease);

    if (librflow_svc_connect(info, ccb) != RFLOW_OK) {
        std::cerr << "svc_connect failed\n";
        librflow_svc_uninit();
        return 1;
    }
    librflow_svc_connect_info_destroy(info);
    librflow_svc_connect_cb_destroy(ccb);

    auto sp = librflow_svc_stream_param_create();
    librflow_svc_stream_param_set_out_codec(sp, RFLOW_CODEC_H264);
    librflow_svc_stream_param_set_src_size(sp, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    librflow_svc_stream_param_set_out_size(sp, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    librflow_svc_stream_param_set_fps(sp, static_cast<uint32_t>(fps));
    librflow_svc_stream_param_set_bitrate(sp, 1500, 2500);
#if defined(__linux__)
    librflow_svc_stream_param_set_video_device_path(sp, camera.c_str());
#else
    librflow_svc_stream_param_set_video_device_index(sp, 0);
#endif

    auto scb = librflow_svc_stream_cb_create();
    librflow_svc_stream_cb_set_on_state(scb, OnStreamState);

    librflow_svc_stream_handle_t stream = nullptr;
    if (librflow_svc_create_stream(stream_idx, sp, scb, &stream) != RFLOW_OK) {
        std::cerr << "svc_create_stream failed\n";
        librflow_svc_stream_param_destroy(sp);
        librflow_svc_stream_cb_destroy(scb);
        librflow_svc_disconnect();
        librflow_svc_uninit();
        return 1;
    }
    librflow_svc_stream_param_destroy(sp);
    librflow_svc_stream_cb_destroy(scb);

    if (librflow_svc_start_stream(stream) != RFLOW_OK) {
        std::cerr << "svc_start_stream failed (check signaling server / camera)\n";
        librflow_svc_destroy_stream(stream);
        librflow_svc_disconnect();
        librflow_svc_uninit();
        return 1;
    }

    std::cout << "[demo] stream_idx=" << stream_idx << " room=" << device_id << ":" << stream_idx << std::endl;
    std::cout << "[demo] SDK internal capture enabled, target "
              << width << "x" << height << "@" << fps << " to " << signaling_url;
#if defined(__linux__)
    std::cout << " camera=" << camera;
#endif
    std::cout << ". Ctrl+C to stop." << std::endl;

    auto last_stats = std::chrono::steady_clock::now();
    while (!rflow::apps::common::StopRequested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_stats).count() >= 5) {
            last_stats = now;
            librflow_stream_stats_t stats = nullptr;
            if (librflow_svc_stream_get_stats(stream, &stats) == RFLOW_OK && stats) {
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
    librflow_svc_stop_stream(stream);
    librflow_svc_destroy_stream(stream);
    librflow_svc_disconnect();
    librflow_svc_uninit();
    return 0;
}

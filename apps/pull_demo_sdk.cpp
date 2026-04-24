/**
 * @file   pull_demo_sdk.cpp
 * @brief  基于 librflow_client C ABI 的拉流 demo（订阅远端 stream，打印帧统计）
 *
 * 用法：
 *   ./pull_demo_sdk <signaling_url> [device_id] [stream_index]
 * 默认：
 *   signaling_url = 127.0.0.1:8765
 *   device_id     = demo_device
 *   stream_index  = 1
 *
 * 依赖：
 *   - 对端 (push_demo_sdk 或兼容 service) 正在以相同 device_id+stream_index 推流；
 *   - 编译时需 -DRFLOW_CLIENT_ENABLE_WEBRTC_IMPL=ON。
 */

#include "rflow/Client/librflow_client_api.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};
std::atomic<uint64_t> g_frames{0};

void OnSig(int) { g_stop.store(true); }

void OnConnectState(rflow_connect_state_t state, rflow_err_t reason, void* /*ud*/) {
    std::cout << "[demo] connect state=" << state << " reason=" << reason << std::endl;
}
void OnStreamState(librflow_stream_handle_t /*h*/, rflow_stream_state_t state,
                    rflow_err_t reason, void* /*ud*/) {
    std::cout << "[demo] stream state=" << state << " reason=" << reason << std::endl;
}
void OnVideoFrame(librflow_stream_handle_t /*h*/, librflow_video_frame_t frame, void* /*ud*/) {
    const uint64_t n = g_frames.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == 1 || n % 30 == 0) {
        const uint32_t w = librflow_video_frame_get_width(frame);
        const uint32_t h = librflow_video_frame_get_height(frame);
        const uint32_t sz = librflow_video_frame_get_data_size(frame);
        std::cout << "[demo] frame#" << n << " " << w << "x" << h
                  << " bytes=" << sz << " codec=" << librflow_video_frame_get_codec(frame)
                  << std::endl;
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string signaling_url = "127.0.0.1:8765";
    std::string device_id     = "demo_device";
    int32_t stream_index      = 1;
    if (argc >= 2) signaling_url = argv[1];
    if (argc >= 3) device_id     = argv[2];
    if (argc >= 4) stream_index  = std::atoi(argv[3]);

    std::signal(SIGINT, OnSig);
    std::signal(SIGTERM, OnSig);

    auto sig_cfg = librflow_signal_config_create();
    librflow_signal_config_set_url(sig_cfg, signaling_url.c_str());
    auto gcfg = librflow_global_config_create();
    librflow_global_config_set_signal(gcfg, sig_cfg);
    if (librflow_set_global_config(gcfg) != RFLOW_OK) {
        std::cerr << "set_global_config failed\n"; return 1;
    }
    librflow_global_config_destroy(gcfg);
    librflow_signal_config_destroy(sig_cfg);

    if (librflow_init() != RFLOW_OK) {
        std::cerr << "init failed\n"; return 1;
    }

    auto info = librflow_connect_info_create();
    librflow_connect_info_set_device_id(info, device_id.c_str());
    librflow_connect_info_set_device_secret(info, "");

    auto ccb = librflow_connect_cb_create();
    librflow_connect_cb_set_on_state(ccb, OnConnectState);

    if (librflow_connect(info, ccb) != RFLOW_OK) {
        std::cerr << "connect failed\n";
        librflow_uninit();
        return 1;
    }
    librflow_connect_info_destroy(info);
    librflow_connect_cb_destroy(ccb);

    auto sp = librflow_stream_param_create();
    librflow_stream_param_set_preferred_codec(sp, RFLOW_CODEC_H264);
    auto scb = librflow_stream_cb_create();
    librflow_stream_cb_set_on_state(scb, OnStreamState);
    librflow_stream_cb_set_on_video(scb, OnVideoFrame);

    librflow_stream_handle_t handle = nullptr;
    if (librflow_open_stream(stream_index, sp, scb, &handle) != RFLOW_OK) {
        std::cerr << "open_stream failed\n";
        librflow_stream_param_destroy(sp);
        librflow_stream_cb_destroy(scb);
        librflow_disconnect();
        librflow_uninit();
        return 1;
    }
    librflow_stream_param_destroy(sp);
    librflow_stream_cb_destroy(scb);

    std::cout << "[demo] pulling stream idx=" << stream_index
              << " from device=" << device_id << ". Ctrl+C to stop." << std::endl;

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "[demo] total frames received: " << g_frames.load() << std::endl;
    librflow_close_stream(handle);
    librflow_disconnect();
    librflow_uninit();
    return 0;
}

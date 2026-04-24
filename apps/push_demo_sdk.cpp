/**
 * @file   push_demo_sdk.cpp
 * @brief  基于 librflow_svc C ABI 的推流 demo（合成 I420 帧 → 服务端编码并发布）
 *
 * 用法：
 *   ./push_demo_sdk <signaling_url> [device_id] [width] [height] [fps]
 * 默认：
 *   signaling_url = 127.0.0.1:8765
 *   device_id     = demo_device
 *   分辨率/帧率  = 640x360 @ 30
 *
 * 依赖运行环境：
 *   - signaling_server 已在 <signaling_url> 启动（apps/signaling_server）
 *   - 编译时需 -DRFLOW_SERVICE_ENABLE_WEBRTC_IMPL=ON
 *
 * 该 demo 生成一个随时间滚动的灰度条纹 I420 帧，验证完整推流链路。
 */

#include "rflow/Service/librflow_service_api.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};

void OnSig(int) { g_stop.store(true); }

void OnConnectState(rflow_connect_state_t state, rflow_err_t reason, void* /*ud*/) {
    std::cout << "[demo] connect state=" << state << " reason=" << reason << std::endl;
}
void OnBindState(rflow_bind_state_t state, const void* /*detail*/, void* /*ud*/) {
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
    std::cout << "[demo] stream state=" << state << " reason=" << reason << std::endl;
}

/// 合成 I420 frame：Y 通道随时间水平滚动条纹，UV 恒定 128（灰度视觉中性）。
void FillSyntheticI420(std::vector<uint8_t>& buf, int w, int h, int tick) {
    const int y_size  = w * h;
    const int uv_w    = (w + 1) / 2;
    const int uv_h    = (h + 1) / 2;
    const int uv_size = uv_w * uv_h;
    buf.resize(static_cast<size_t>(y_size + 2 * uv_size));
    uint8_t* y  = buf.data();
    uint8_t* u  = y + y_size;
    uint8_t* v  = u + uv_size;
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            y[row * w + col] = static_cast<uint8_t>((col + tick) % 256);
        }
    }
    std::memset(u, 128, uv_size);
    std::memset(v, 128, uv_size);
}

}  // namespace

int main(int argc, char** argv) {
    std::string signaling_url = "127.0.0.1:8765";
    std::string device_id     = "demo_device";
    int width  = 640;
    int height = 360;
    int fps    = 30;
    if (argc >= 2) signaling_url = argv[1];
    if (argc >= 3) device_id     = argv[2];
    if (argc >= 4) width  = std::atoi(argv[3]);
    if (argc >= 5) height = std::atoi(argv[4]);
    if (argc >= 6) fps    = std::atoi(argv[5]);

    std::signal(SIGINT, OnSig);
    std::signal(SIGTERM, OnSig);

    // ---------- global config ---------- //
    auto sig_cfg = librflow_signal_config_create();
    librflow_signal_config_set_url(sig_cfg, signaling_url.c_str());
    auto gcfg = librflow_global_config_create();
    librflow_global_config_set_signal(gcfg, sig_cfg);

    if (librflow_svc_set_global_config(gcfg) != RFLOW_OK) {
        std::cerr << "svc_set_global_config failed\n"; return 1;
    }
    librflow_global_config_destroy(gcfg);
    librflow_signal_config_destroy(sig_cfg);

    if (librflow_svc_init() != RFLOW_OK) {
        std::cerr << "svc_init failed\n"; return 1;
    }

    // ---------- connect ---------- //
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

    // ---------- create / start stream ---------- //
    auto sp = librflow_svc_stream_param_create();
    librflow_svc_stream_param_set_in_codec(sp, RFLOW_CODEC_I420);
    librflow_svc_stream_param_set_out_codec(sp, RFLOW_CODEC_H264);
    librflow_svc_stream_param_set_src_size(sp, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    librflow_svc_stream_param_set_out_size(sp, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    librflow_svc_stream_param_set_fps(sp, static_cast<uint32_t>(fps));
    librflow_svc_stream_param_set_bitrate(sp, 1500, 2500);

    auto scb = librflow_svc_stream_cb_create();
    librflow_svc_stream_cb_set_on_state(scb, OnStreamState);

    librflow_svc_stream_handle_t stream = nullptr;
    const int32_t stream_idx = 1;
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
        std::cerr << "svc_start_stream failed (check signaling server)\n";
        librflow_svc_destroy_stream(stream);
        librflow_svc_disconnect();
        librflow_svc_uninit();
        return 1;
    }

    // ---------- push loop ---------- //
    std::cout << "[demo] pushing I420 " << width << "x" << height << "@" << fps
              << " fps to " << signaling_url << ". Ctrl+C to stop." << std::endl;

    std::vector<uint8_t> buf;
    const auto frame_period = std::chrono::microseconds(1'000'000 / (fps > 0 ? fps : 30));
    int tick = 0;
    const auto start = std::chrono::steady_clock::now();
    while (!g_stop.load()) {
        FillSyntheticI420(buf, width, height, tick++);

        auto fr = librflow_svc_push_frame_create();
        librflow_svc_push_frame_set_codec(fr, RFLOW_CODEC_I420);
        librflow_svc_push_frame_set_type(fr, RFLOW_FRAME_I);
        librflow_svc_push_frame_set_size(fr, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count();
        librflow_svc_push_frame_set_pts_ms(fr, static_cast<uint64_t>(now_us / 1000));
        librflow_svc_push_frame_set_data(fr, buf.data(), static_cast<uint32_t>(buf.size()));

        rflow_err_t err = librflow_svc_push_video_frame(stream, fr);
        librflow_svc_push_frame_destroy(fr);
        if (err != RFLOW_OK) {
            std::cerr << "push_video_frame err=" << err << std::endl;
        }
        std::this_thread::sleep_for(frame_period);
    }

    std::cout << "[demo] stopping..." << std::endl;
    librflow_svc_stop_stream(stream);
    librflow_svc_destroy_stream(stream);
    librflow_svc_disconnect();
    librflow_svc_uninit();
    return 0;
}

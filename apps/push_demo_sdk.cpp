/**
 * @file   push_demo_sdk.cpp
 * @brief  基于 librflow_svc C ABI 的推流 demo
 *
 * 用法：
 *   ./push_demo_sdk <signaling_url> [device_id] [width] [height] [fps] [stream_idx]
 * 默认：
 *   signaling_url = 127.0.0.1:8765
 *   device_id     = demo_device
 *   分辨率/帧率  = 640x360 @ 30
 *   stream_idx    = 0  （信令 room = device_id + ":" + stream_idx，需与拉流端 open_stream 索引一致）
 *
 * Linux：默认从 V4L2 采集真实画面（当前设备路径见下方 kPushDemoV4l2Device，写死为联调用）。
 *        若需条纹测试画面：export RFLOW_PUSH_DEMO_SYNTHETIC=1
 *
 * 依赖运行环境：
 *   - signaling_server 已在 <signaling_url> 启动（apps/signaling_server）
 *   - 编译时需 -DRFLOW_SERVICE_ENABLE_WEBRTC_IMPL=ON
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

#if defined(__linux__)
#  include <errno.h>
#  include <fcntl.h>
#  include <poll.h>
#  include <sys/ioctl.h>
#  include <sys/mman.h>
#  include <unistd.h>
#  include <linux/videodev2.h>
#endif

namespace {

std::atomic<bool> g_stop{false};

void OnSig(int) { g_stop.store(true); }

void OnConnectState(rflow_connect_state_t state, rflow_err_t reason, void* /*ud*/) {
    std::cout << "[demo] connect state=" << state << " reason=" << reason << std::endl;
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
    std::cout << "[demo] stream state=" << state << " reason=" << reason << std::endl;
}

// TODO(libRoboFlow): 从 argv（如 --camera）或 streams.conf 的 STREAM_<stream_id>_CAMERA 读取设备路径，
// 与 push_demo / 配置体系对齐；当前为板端联调写死。
#if defined(__linux__)
constexpr const char* kPushDemoV4l2Device = "/dev/video0";
#endif

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

#if defined(__linux__)
/// YUYV 4:2:2 → I420（src_stride 为 V4L2 bytesperline）。
void Yuyv422ToI420(const uint8_t* src, int src_stride, int w, int h, std::vector<uint8_t>& i420) {
    const int y_size  = w * h;
    const int uv_w    = (w + 1) / 2;
    const int uv_h    = (h + 1) / 2;
    const int uv_size = uv_w * uv_h;
    i420.resize(static_cast<size_t>(y_size + 2 * uv_size));
    uint8_t* y_dst = i420.data();
    uint8_t* u_dst = y_dst + y_size;
    uint8_t* v_dst = u_dst + uv_size;
    for (int row = 0; row + 1 < h; row += 2) {
        const uint8_t* row0 = src + row * src_stride;
        const uint8_t* row1 = src + (row + 1) * src_stride;
        for (int col = 0; col + 1 < w; col += 2) {
            const int x = col * 2;
            y_dst[row * w + col]         = row0[x + 0];
            y_dst[row * w + col + 1]     = row0[x + 2];
            y_dst[(row + 1) * w + col]     = row1[x + 0];
            y_dst[(row + 1) * w + col + 1] = row1[x + 2];
            const int u_avg = (static_cast<int>(row0[x + 1]) + static_cast<int>(row1[x + 1]) + 1) / 2;
            const int v_avg = (static_cast<int>(row0[x + 3]) + static_cast<int>(row1[x + 3]) + 1) / 2;
            const int ux    = col / 2;
            const int uy    = row / 2;
            u_dst[uy * uv_w + ux] = static_cast<uint8_t>(u_avg);
            v_dst[uy * uv_w + ux] = static_cast<uint8_t>(v_avg);
        }
    }
}

void PackNv12Contiguous(const uint8_t* src, int src_bpl, int w, int h, std::vector<uint8_t>& out) {
    const int y_sz  = w * h;
    const int uv_h  = (h + 1) / 2;
    const int total = y_sz + w * uv_h;
    out.resize(static_cast<size_t>(total));
    uint8_t* dst_y  = out.data();
    uint8_t* dst_uv = dst_y + y_sz;
    for (int row = 0; row < h; ++row) {
        std::memcpy(dst_y + row * w, src + row * src_bpl, static_cast<size_t>(w));
    }
    const uint8_t* src_uv = src + src_bpl * h;
    for (int row = 0; row < uv_h; ++row) {
        std::memcpy(dst_uv + row * w, src_uv + row * src_bpl, static_cast<size_t>(w));
    }
}

struct V4l2Capture {
    enum class Pix { None, Yuyv, Nv12 };

    int fd{-1};
    Pix pix{Pix::None};
    int width{0};
    int height{0};
    uint32_t bytesperline{0};
    std::vector<void*> mmap_ptrs;
    std::vector<size_t> mmap_lens;

    void Close() {
        if (fd >= 0) {
            const v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(fd, VIDIOC_STREAMOFF, &type);
            for (size_t i = 0; i < mmap_ptrs.size(); ++i) {
                if (mmap_ptrs[i] && mmap_lens[i]) {
                    munmap(mmap_ptrs[i], mmap_lens[i]);
                }
            }
            mmap_ptrs.clear();
            mmap_lens.clear();
            ::close(fd);
            fd = -1;
        }
        pix = Pix::None;
    }

    ~V4l2Capture() { Close(); }

    bool Open(const char* path, int req_w, int req_h) {
        fd = ::open(path, O_RDWR | O_NONBLOCK, 0);
        if (fd < 0) {
            std::cerr << "[demo] V4L2 open " << path << " failed: " << std::strerror(errno) << std::endl;
            return false;
        }

        v4l2_format fmt{};
        fmt.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width        = static_cast<uint32_t>(req_w);
        fmt.fmt.pix.height       = static_cast<uint32_t>(req_h);
        fmt.fmt.pix.field        = V4L2_FIELD_ANY;
        fmt.fmt.pix.pixelformat  = V4L2_PIX_FMT_YUYV;
        if (ioctl(fd, VIDIOC_S_FMT, &fmt) != 0) {
            fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
            if (ioctl(fd, VIDIOC_S_FMT, &fmt) != 0) {
                std::cerr << "[demo] V4L2 S_FMT YUYV/NV12 failed: " << std::strerror(errno) << std::endl;
                Close();
                return false;
            }
            pix = Pix::Nv12;
        } else {
            pix = Pix::Yuyv;
        }

        width         = static_cast<int>(fmt.fmt.pix.width);
        height        = static_cast<int>(fmt.fmt.pix.height);
        bytesperline  = fmt.fmt.pix.bytesperline;
        if (width <= 0 || height <= 0 || bytesperline == 0) {
            std::cerr << "[demo] V4L2 invalid size after S_FMT\n";
            Close();
            return false;
        }

        v4l2_requestbuffers rb{};
        rb.count  = 4;
        rb.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        rb.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_REQBUFS, &rb) != 0 || rb.count < 2) {
            std::cerr << "[demo] V4L2 REQBUFS failed: " << std::strerror(errno) << std::endl;
            Close();
            return false;
        }

        mmap_ptrs.assign(rb.count, nullptr);
        mmap_lens.assign(rb.count, 0);
        for (uint32_t i = 0; i < rb.count; ++i) {
            v4l2_buffer buf{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = i;
            if (ioctl(fd, VIDIOC_QUERYBUF, &buf) != 0) {
                Close();
                return false;
            }
            void* p = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                           static_cast<off_t>(buf.m.offset));
            if (p == MAP_FAILED) {
                std::cerr << "[demo] V4L2 mmap failed\n";
                Close();
                return false;
            }
            mmap_ptrs[i] = p;
            mmap_lens[i] = buf.length;
        }

        for (uint32_t i = 0; i < rb.count; ++i) {
            v4l2_buffer buf{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = i;
            if (ioctl(fd, VIDIOC_QBUF, &buf) != 0) {
                Close();
                return false;
            }
        }

        const v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMON, &type) != 0) {
            std::cerr << "[demo] V4L2 STREAMON failed: " << std::strerror(errno) << std::endl;
            Close();
            return false;
        }

        std::cout << "[demo] V4L2 " << path << " " << width << "x" << height
                  << " fmt=" << (pix == Pix::Yuyv ? "YUYV" : "NV12") << " bpl=" << bytesperline
                  << std::endl;
        return true;
    }

    bool Dequeue(void** out_data, uint32_t* out_index) {
        pollfd p{};
        p.fd     = fd;
        p.events = POLLIN;
        if (poll(&p, 1, 3000) <= 0) {
            return false;
        }
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_DQBUF, &buf) != 0) {
            return false;
        }
        *out_data  = mmap_ptrs[buf.index];
        *out_index = buf.index;
        return *out_data != nullptr;
    }

    void Enqueue(uint32_t index) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = index;
        ioctl(fd, VIDIOC_QBUF, &buf);
    }
};
#endif  // __linux__

}  // namespace

int main(int argc, char** argv) {
    std::string signaling_url = "127.0.0.1:8765";
    std::string device_id     = "demo_device";
    int width  = 640;
    int height = 360;
    int fps    = 30;
    rflow_stream_index_t stream_idx = 0;
    if (argc >= 2) signaling_url = argv[1];
    if (argc >= 3) device_id     = argv[2];
    if (argc >= 4) width  = std::atoi(argv[3]);
    if (argc >= 5) height = std::atoi(argv[4]);
    if (argc >= 6) fps    = std::atoi(argv[5]);
    if (argc >= 7) stream_idx = static_cast<rflow_stream_index_t>(std::atoi(argv[6]));

    std::signal(SIGINT, OnSig);
    std::signal(SIGTERM, OnSig);

#if defined(__linux__)
    const bool force_synthetic =
        std::getenv("RFLOW_PUSH_DEMO_SYNTHETIC") != nullptr &&
        std::getenv("RFLOW_PUSH_DEMO_SYNTHETIC")[0] != '\0' &&
        std::strcmp(std::getenv("RFLOW_PUSH_DEMO_SYNTHETIC"), "0") != 0;
    V4l2Capture v4l;
    bool use_v4l = false;
    rflow_codec_t push_codec = RFLOW_CODEC_I420;
    if (!force_synthetic && v4l.Open(kPushDemoV4l2Device, width, height)) {
        use_v4l    = true;
        width      = v4l.width;
        height     = v4l.height;
        push_codec = (v4l.pix == V4l2Capture::Pix::Nv12) ? RFLOW_CODEC_NV12 : RFLOW_CODEC_I420;
        std::cout << "[demo] using camera capture (RFLOW_PUSH_DEMO_SYNTHETIC=1 to force stripes)\n";
    } else {
        if (!force_synthetic) {
            std::cerr << "[demo] camera open failed, fallback to synthetic I420\n";
        } else {
            std::cout << "[demo] RFLOW_PUSH_DEMO_SYNTHETIC: synthetic I420 stripes\n";
        }
        push_codec = RFLOW_CODEC_I420;
    }
#else
    const bool use_v4l      = false;
    const rflow_codec_t push_codec = RFLOW_CODEC_I420;
#endif

    // ---------- global config ---------- //
    auto sig_cfg = librflow_signal_config_create();
    librflow_signal_config_set_url(sig_cfg, signaling_url.c_str());
    auto gcfg = librflow_global_config_create();
    librflow_global_config_set_signal(gcfg, sig_cfg);

    if (librflow_svc_set_global_config(gcfg) != RFLOW_OK) {
        std::cerr << "svc_set_global_config failed\n";
#if defined(__linux__)
        v4l.Close();
#endif
        return 1;
    }
    librflow_global_config_destroy(gcfg);
    librflow_signal_config_destroy(sig_cfg);

    if (librflow_svc_init() != RFLOW_OK) {
        std::cerr << "svc_init failed\n";
#if defined(__linux__)
        v4l.Close();
#endif
        return 1;
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
#if defined(__linux__)
        v4l.Close();
#endif
        return 1;
    }
    librflow_svc_connect_info_destroy(info);
    librflow_svc_connect_cb_destroy(ccb);

    // ---------- create / start stream ---------- //
    auto sp = librflow_svc_stream_param_create();
    librflow_svc_stream_param_set_in_codec(sp, push_codec);
    librflow_svc_stream_param_set_out_codec(sp, RFLOW_CODEC_H264);
    librflow_svc_stream_param_set_src_size(sp, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    librflow_svc_stream_param_set_out_size(sp, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    librflow_svc_stream_param_set_fps(sp, static_cast<uint32_t>(fps));
    librflow_svc_stream_param_set_bitrate(sp, 1500, 2500);

    auto scb = librflow_svc_stream_cb_create();
    librflow_svc_stream_cb_set_on_state(scb, OnStreamState);

    librflow_svc_stream_handle_t stream = nullptr;
    if (librflow_svc_create_stream(stream_idx, sp, scb, &stream) != RFLOW_OK) {
        std::cerr << "svc_create_stream failed\n";
        librflow_svc_stream_param_destroy(sp);
        librflow_svc_stream_cb_destroy(scb);
        librflow_svc_disconnect();
        librflow_svc_uninit();
#if defined(__linux__)
        v4l.Close();
#endif
        return 1;
    }
    librflow_svc_stream_param_destroy(sp);
    librflow_svc_stream_cb_destroy(scb);

    if (librflow_svc_start_stream(stream) != RFLOW_OK) {
        std::cerr << "svc_start_stream failed (check signaling server)\n";
        librflow_svc_destroy_stream(stream);
        librflow_svc_disconnect();
        librflow_svc_uninit();
#if defined(__linux__)
        v4l.Close();
#endif
        return 1;
    }

    // ---------- push loop ---------- //
    std::cout << "[demo] stream_idx=" << stream_idx << " room=" << device_id << ":" << stream_idx << std::endl;
    std::cout << "[demo] pushing " << (use_v4l ? "V4L2" : "synthetic I420") << " " << width << "x" << height << "@"
              << fps << " to " << signaling_url << ". Ctrl+C to stop." << std::endl;

    std::vector<uint8_t> buf;
    std::vector<uint8_t> packed;
    const auto frame_period = std::chrono::microseconds(1'000'000 / (fps > 0 ? fps : 30));
    int tick = 0;
    const auto start = std::chrono::steady_clock::now();
    while (!g_stop.load()) {
#if defined(__linux__)
        if (use_v4l) {
            void* cap_data = nullptr;
            uint32_t cap_index = 0;
            if (!v4l.Dequeue(&cap_data, &cap_index)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            const auto* src = static_cast<const uint8_t*>(cap_data);
            if (v4l.pix == V4l2Capture::Pix::Yuyv) {
                Yuyv422ToI420(src, static_cast<int>(v4l.bytesperline), v4l.width, v4l.height, buf);
            } else {
                if (static_cast<int>(v4l.bytesperline) == v4l.width) {
                    buf.assign(src, src + static_cast<size_t>(v4l.width * v4l.height +
                                                               v4l.width * ((v4l.height + 1) / 2)));
                } else {
                    PackNv12Contiguous(src, static_cast<int>(v4l.bytesperline), v4l.width, v4l.height,
                                       packed);
                    buf.swap(packed);
                }
            }
            v4l.Enqueue(cap_index);
        } else
#endif
        {
            FillSyntheticI420(buf, width, height, tick++);
        }

        auto fr = librflow_svc_push_frame_create();
        librflow_svc_push_frame_set_codec(fr, use_v4l ? push_codec : RFLOW_CODEC_I420);
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
#if defined(__linux__)
        if (!use_v4l)
#endif
        {
            std::this_thread::sleep_for(frame_period);
        }
    }

    std::cout << "[demo] stopping..." << std::endl;
    librflow_svc_stop_stream(stream);
    librflow_svc_destroy_stream(stream);
    librflow_svc_disconnect();
    librflow_svc_uninit();
#if defined(__linux__)
    v4l.Close();
#endif
    return 0;
}

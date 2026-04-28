#define MODULE_TAG "rflow_mpp_mjpeg"

#include "core/rtc/hw/rockchip_mpp/mjpeg_decoder.h"

#include "core/rtc/hw/rockchip_mpp/native_dec_frame_buffer.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <limits>
#include <unistd.h>

#include <linux/dma-buf.h>
#include <sys/ioctl.h>

#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "rtc_base/time_utils.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/planar_functions.h"

#include "mpp_buffer.h"
#include "mpp_err.h"
#include "mpp_frame.h"
#include "mpp_meta.h"
#include "mpp_packet.h"
#include "rk_mpi.h"
#include "rk_type.h"
#include "rk_vdec_cfg.h"

#define MPP_ALIGN(x, a) (((x) + ((a)-1)) & ~((a)-1))

#if defined(RFLOW_HAVE_LIBRGA)
#include <rga/im2d.hpp>
#include <rga/rga.h>
#endif

namespace rflow::rtc::hw::rockchip_mpp {

namespace {

static bool MjpegDecTraceEnabled() {
    static const char* k = std::getenv("WEBRTC_MJPEG_DEC_TRACE");
    return k && k[0] != '0';
}

static bool LatencyTraceEnabled() {
    static int cached = -1;
    if (cached >= 0) {
        return cached != 0;
    }
    const char* e = std::getenv("WEBRTC_LATENCY_TRACE");
    cached = (e && e[0] == '1') ? 1 : 0;
    return cached != 0;
}

static bool MjpegDecLowLatency() {
    static int cached = -1;
    if (cached >= 0) {
        return cached != 0;
    }
    const char* e = std::getenv("WEBRTC_MJPEG_DEC_LOW_LATENCY");
    cached = (e && (e[0] == '1' || e[0] == 'y' || e[0] == 'Y')) ? 1 : 0;
    return cached != 0;
}

static int MjpegPollOutputTimeoutMs() {
    return MjpegDecLowLatency() ? 8 : 30;
}

static int MjpegPollSleepUs() {
    return MjpegDecLowLatency() ? 200 : 2000;
}

#if defined(RFLOW_HAVE_LIBRGA)

static bool RgaDisableAfterFailEnabled() {
    const char* e = std::getenv("WEBRTC_MJPEG_RGA_DISABLE_AFTER_FAIL");
    return e && (e[0] == '1' || e[0] == 'y' || e[0] == 'Y');
}

/// Maximum allowed aspect ratio used for RGA Y400 copy rectangle.
/// Both (w / h) and (h / w) must be <= this value.
/// Can be tightened via WEBRTC_MJPEG_RGA_MAX_ASPECT, for example 16.
static int RgaY400MaxAspectRatio() {
    static int cached = -1;
    if (cached >= 0) {
        return cached;
    }
    const char* e = std::getenv("WEBRTC_MJPEG_RGA_MAX_ASPECT");
    if (e && e[0] != '\0') {
        const long v = std::strtol(e, nullptr, 10);
        if (v >= 2 && v <= 256) {
            cached = static_cast<int>(v);
            return cached;
        }
    }
    cached = 64;
    return cached;
}

/// Select w*h for RK_FORMAT_YCbCr_400 RGA imcopy (1 byte per pixel), with need<=w*h<=cap.
///
/// Note: w/h here are not decoded image dimensions. JPEG is still compressed bitstream.
/// We treat jpeg_len bytes as a 2D fake-gray rectangle only to satisfy RGA rectangle API.
/// Old approach could pick extreme strips (e.g. 4096x64) and trigger EINVAL on some BSPs.
/// So we choose a valid pair with aspect ratio closest to 1, still bounded by
/// WEBRTC_MJPEG_RGA_MAX_ASPECT.
static bool RgaPickY400Rect(size_t cap, size_t need, int* out_w, int* out_h) {
    if (need == 0 || need > cap || !out_w || !out_h) {
        return false;
    }
    constexpr int kMinW = 64;
    constexpr int kMinH = 16;
    constexpr int kMaxW = 4096;
    constexpr int kMaxH = 8192;
    constexpr int kAlignW = 16;
    const int kMaxAspectRatio = RgaY400MaxAspectRatio();

    const int w_hi =
        std::min(kMaxW, static_cast<int>(std::min(cap / static_cast<size_t>(kMinH), static_cast<size_t>(kMaxW))));

    int best_w = 0;
    int best_h = 0;
    size_t best_total = SIZE_MAX;

    for (int w = kMinW; w <= w_hi; w += kAlignW) {
        const size_t wz = static_cast<size_t>(w);
        size_t h = (need + wz - 1u) / wz;
        const size_t h_min_for_landscape =
            (wz + static_cast<size_t>(kMaxAspectRatio) - 1u) / static_cast<size_t>(kMaxAspectRatio);
        h = std::max({h, h_min_for_landscape, static_cast<size_t>(kMinH)});
        if ((h & 1u) != 0u) {
            ++h;
        }
        if (h > static_cast<size_t>(kMaxH)) {
            continue;
        }
        if (h > wz * static_cast<size_t>(kMaxAspectRatio)) {
            continue;
        }
        size_t total = wz * h;
        if (total < need) {
            h = (need + wz - 1u) / wz;
            h = std::max({h, h_min_for_landscape, static_cast<size_t>(kMinH)});
            if ((h & 1u) != 0u) {
                ++h;
            }
            if (h > static_cast<size_t>(kMaxH) || h > wz * static_cast<size_t>(kMaxAspectRatio)) {
                continue;
            }
            total = wz * h;
        }
        if (total < need || total > cap) {
            continue;
        }
        const int hi = static_cast<int>(h);
        const int tmn = std::min(w, hi);
        const int tmx = std::max(w, hi);
        const int bmn = std::min(best_w, best_h);
        const int bmx = std::max(best_w, best_h);
        // Minimize aspect ratio tmx/tmn. For positive integers:
        // tmx/tmn < bmx/bmn  <=>  tmx*bmn < bmx*tmn.
        const auto lhs = static_cast<unsigned long long>(tmx) * static_cast<unsigned long long>(bmn);
        const auto rhs = static_cast<unsigned long long>(bmx) * static_cast<unsigned long long>(tmn);
        if (best_w == 0 || lhs < rhs || (lhs == rhs && total < best_total)) {
            best_w = w;
            best_h = hi;
            best_total = total;
        }
    }

    if (best_w == 0) {
        return false;
    }
    *out_w = best_w;
    *out_h = best_h;
    return true;
}

/// Smallest total bytes for a rectangle covering `need` without cap upper bound.
/// Used to validate V4L2 DMA size and MPP input allocation size.
static size_t RgaY400SmallestTotalBytes(size_t need) {
    int w = 0;
    int h = 0;
    if (!RgaPickY400Rect(std::numeric_limits<size_t>::max(), need, &w, &h)) {
        return need;
    }
    return static_cast<size_t>(w) * static_cast<size_t>(h);
}

/// RGA copy from UVC vb2 dma-buf to MPP DRM dma-buf.
///
/// Common failure roots (not related to MJPEG image dimensions):
/// 1) Registering fd without real size. Some paths cannot infer accessible size from vb2/DRM gem.
///    Prefer importbuffer_fd(fd, size_bytes) then wrapbuffer_handle.
/// 2) Address space limits. Some BSPs require DMA below 4G.
/// 3) Cross-device allocator incompatibility.
///
/// Strategy: prefer import+wrapbuffer_handle, fallback to wrapbuffer_fd.
static bool RgaCopyDmaBufJpeg(int src_fd,
                              size_t src_cap,
                              int dst_fd,
                              size_t dst_cap,
                              size_t jpeg_len) {
    if (src_fd < 0 || dst_fd < 0 || jpeg_len == 0) {
        return false;
    }
    const size_t cap = std::min(src_cap, dst_cap);
    if (jpeg_len > cap) {
        return false;
    }
    int w = 0;
    int h = 0;
    if (!RgaPickY400Rect(cap, jpeg_len, &w, &h)) {
        if (MjpegDecTraceEnabled()) {
            std::cerr << "[RkMppMjpeg] RGA layout failed need=" << jpeg_len << " cap=" << cap << "\n";
        }
        return false;
    }

    {
        struct dma_buf_sync sync {};
        sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ;
        if (ioctl(src_fd, DMA_BUF_IOCTL_SYNC, &sync) != 0 && MjpegDecTraceEnabled()) {
            std::cerr << "[RkMppMjpeg] RGA src DMA_BUF_SYNC(READ) errno=" << errno << "\n";
        }
    }
    {
        struct dma_buf_sync sync {};
        sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE;
        if (ioctl(dst_fd, DMA_BUF_IOCTL_SYNC, &sync) != 0 && MjpegDecTraceEnabled()) {
            std::cerr << "[RkMppMjpeg] RGA dst DMA_BUF_SYNC(WRITE) errno=" << errno << "\n";
        }
    }

    const int src_import_sz =
        static_cast<int>(std::min(src_cap, static_cast<size_t>(std::numeric_limits<int>::max())));
    const int dst_import_sz =
        static_cast<int>(std::min(dst_cap, static_cast<size_t>(std::numeric_limits<int>::max())));

    auto do_end_sync = [&]() {
        {
            struct dma_buf_sync sync {};
            sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
            if (ioctl(dst_fd, DMA_BUF_IOCTL_SYNC, &sync) != 0 && MjpegDecTraceEnabled()) {
                std::cerr << "[RkMppMjpeg] RGA dst DMA_BUF_SYNC(END WRITE) errno=" << errno << "\n";
            }
        }
        {
            struct dma_buf_sync sync {};
            sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
            if (ioctl(src_fd, DMA_BUF_IOCTL_SYNC, &sync) != 0 && MjpegDecTraceEnabled()) {
                std::cerr << "[RkMppMjpeg] RGA src DMA_BUF_SYNC(END READ) errno=" << errno << "\n";
            }
        }
    };

    IM_STATUS st = IM_STATUS_FAILED;
    rga_buffer_handle_t hs = 0;
    rga_buffer_handle_t hd = 0;
    hs = importbuffer_fd(src_fd, src_import_sz);
    hd = importbuffer_fd(dst_fd, dst_import_sz);
    if (hs != 0 && hd != 0) {
        rga_buffer_t src = wrapbuffer_handle(hs, w, h, RK_FORMAT_YCbCr_400);
        rga_buffer_t dst = wrapbuffer_handle(hd, w, h, RK_FORMAT_YCbCr_400);
        st = imcopy(src, dst, 1);
    }
    if (hs != 0) {
        releasebuffer_handle(hs);
        hs = 0;
    }
    if (hd != 0) {
        releasebuffer_handle(hd);
        hd = 0;
    }

    if (st != IM_STATUS_SUCCESS && st != IM_STATUS_NOERROR) {
        if (MjpegDecTraceEnabled()) {
            std::cerr << "[RkMppMjpeg] RGA imcopy(importbuffer) status=" << static_cast<int>(st) << " ("
                      << imStrError_t(st) << "), retry wrapbuffer_fd\n";
        }
        rga_buffer_t src = wrapbuffer_fd(src_fd, w, h, RK_FORMAT_YCbCr_400);
        rga_buffer_t dst = wrapbuffer_fd(dst_fd, w, h, RK_FORMAT_YCbCr_400);
        st = imcopy(src, dst, 1);
    }

    do_end_sync();

    if (st != IM_STATUS_SUCCESS && st != IM_STATUS_NOERROR) {
        if (MjpegDecTraceEnabled()) {
            std::cerr << "[RkMppMjpeg] imcopy failed status=" << static_cast<int>(st) << " (" << imStrError_t(st) << ")\n";
        }
        return false;
    }
    return true;
}
#endif  // RFLOW_HAVE_LIBRGA

static constexpr int kMppBufferGroupIndex = 1;

/// Copy MPP NV12/NV21 frame into WebRTC NV12Buffer row-by-row.
/// This avoids full-block copy assumptions on stride.
static bool CopyMppSemiPlanarToNv12(RK_U32 fmt,
                                    const uint8_t* src_y,
                                    const uint8_t* src_uv,
                                    int src_stride,
                                    int width,
                                    int height,
                                    webrtc::NV12Buffer* out) {
    if (!out || width <= 0 || height <= 0) {
        return false;
    }
    uint8_t* dst_y = out->MutableDataY();
    uint8_t* dst_uv = out->MutableDataUV();
    const int dst_sy = out->StrideY();
    const int dst_suv = out->StrideUV();
    if (fmt == MPP_FMT_YUV420SP) {
        for (int r = 0; r < height; ++r) {
            std::memcpy(dst_y + r * dst_sy, src_y + r * src_stride, static_cast<size_t>(width));
        }
        for (int r = 0; r < height / 2; ++r) {
            std::memcpy(dst_uv + r * dst_suv, src_uv + r * src_stride, static_cast<size_t>(width));
        }
        return true;
    }
    if (fmt == MPP_FMT_YUV420SP_VU) {
        return libyuv::NV21ToNV12(src_y, src_stride, src_uv, src_stride, dst_y, dst_sy, dst_uv, dst_suv, width,
                                  height) == 0;
    }
    return false;
}

}  // namespace

bool RkMppMjpegDecoder::WantExtDmabufImport() const {
    if (const char* e = std::getenv("WEBRTC_MJPEG_V4L2_DMABUF")) {
        return e[0] != '0';
    }
    return pipeline_v4l2_ext_dma_;
}

bool RkMppMjpegDecoder::WantRgaToMpp() const {
#if !defined(RFLOW_HAVE_LIBRGA)
    return false;
#else
    if (const char* e = std::getenv("WEBRTC_MJPEG_RGA_TO_MPP")) {
        return e[0] != '0';
    }
    return pipeline_rga_to_mpp_;
#endif
}

RkMppMjpegDecoder::RkMppMjpegDecoder() = default;

RkMppMjpegDecoder::~RkMppMjpegDecoder() {
    Close();
}

void RkMppMjpegDecoder::Close() {
    if (dec_cfg_) {
        mpp_dec_cfg_deinit(reinterpret_cast<MppDecCfg>(dec_cfg_));
        dec_cfg_ = nullptr;
    }
    if (input_group_) {
        mpp_buffer_group_put(reinterpret_cast<MppBufferGroup>(input_group_));
        input_group_ = nullptr;
    }
    if (output_buf_group_) {
        mpp_buffer_group_put(reinterpret_cast<MppBufferGroup>(output_buf_group_));
        output_buf_group_ = nullptr;
    }
    if (ctx_) {
        mpp_destroy(reinterpret_cast<MppCtx>(ctx_));
        ctx_ = nullptr;
        mpi_ = nullptr;
    }
    last_expect_w_ = last_expect_h_ = 0;
    output_buf_size_ = 0;
    session_skip_rga_ = false;
    pipeline_v4l2_ext_dma_ = false;
    pipeline_rga_to_mpp_ = false;
}

size_t RkMppMjpegDecoder::ComputeJpegOutputBufSize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    // ?? gstmppjpegdec.c??NV12 ?? + MAX(size, plane1_offset*2)
    const RK_U32 hor = MPP_ALIGN(static_cast<RK_U32>(width), 16);
    const RK_U32 ver = MPP_ALIGN(static_cast<RK_U32>(height), 16);
    const RK_U32 plane0 = hor * ver;
    const RK_U32 nv12 = plane0 + plane0 / 2;
    return static_cast<size_t>(std::max(nv12, plane0 * 2u));
}

bool RkMppMjpegDecoder::Init() {
    Close();

    MppBufferGroup output_grp = nullptr;
    MppBufferGroup input_grp = nullptr;
    if (mpp_buffer_group_get_internal(&output_grp, MPP_BUFFER_TYPE_DRM) != MPP_OK || !output_grp) {
        std::cerr << "[RkMppMjpeg] output mpp_buffer_group_get_internal(DRM) failed\n";
        return false;
    }
    if (mpp_buffer_group_get_internal(&input_grp, MPP_BUFFER_TYPE_DRM) != MPP_OK || !input_grp) {
        std::cerr << "[RkMppMjpeg] input mpp_buffer_group_get_internal(DRM) failed\n";
        mpp_buffer_group_put(output_grp);
        return false;
    }
    output_buf_group_ = output_grp;
    input_group_ = input_grp;

    MppCtx ctx = nullptr;
    MppApi* mpi = nullptr;
    if (mpp_create(&ctx, &mpi) != MPP_OK || !ctx || !mpi) {
        std::cerr << "[RkMppMjpeg] mpp_create failed\n";
        Close();
        return false;
    }
    ctx_ = ctx;
    mpi_ = mpi;

    RK_S64 out_timeout = MPP_POLL_BLOCK;
    mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &out_timeout);

    // Same as gstmppdec.c: set before mpp_init.
    RK_U32 fast_mode = 1;
    mpi->control(ctx, MPP_DEC_SET_PARSER_FAST_MODE, &fast_mode);

    if (mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG) != MPP_OK) {
        std::cerr << "[RkMppMjpeg] mpp_init(MJPEG) failed\n";
        Close();
        return false;
    }

    // Same as gstmppdec.c set_format: bind allocator group right after mpp_init.
    if (mpi->control(ctx, MPP_DEC_SET_EXT_BUF_GROUP, output_grp) != MPP_OK) {
        std::cerr << "[RkMppMjpeg] MPP_DEC_SET_EXT_BUF_GROUP failed\n";
        Close();
        return false;
    }

    MppDecCfg cfg = nullptr;
    if (mpp_dec_cfg_init(&cfg) != MPP_OK || !cfg) {
        Close();
        return false;
    }
    dec_cfg_ = cfg;
    if (mpi->control(ctx, MPP_DEC_GET_CFG, cfg) != MPP_OK) {
        Close();
        return false;
    }
    mpp_dec_cfg_set_u32(cfg, "base:split_parse", 0);
    if (mpi->control(ctx, MPP_DEC_SET_CFG, cfg) != MPP_OK) {
        Close();
        return false;
    }

    MppFrameFormat want = MPP_FMT_YUV420SP;
    if (mpi->control(ctx, MPP_DEC_SET_OUTPUT_FORMAT, &want) != MPP_OK) {
        if (MjpegDecTraceEnabled()) {
            std::cerr << "[RkMppMjpeg] MPP_DEC_SET_OUTPUT_FORMAT(NV12) failed (non-fatal)\n";
        }
    }

    if (MjpegDecTraceEnabled()) {
        std::cerr << "[RkMppMjpeg] Init OK (GStreamer-style: DRM groups + EXT_BUF_GROUP at init + task path)\n";
    }
    return true;
}

bool RkMppMjpegDecoder::BuildMppInputPacket(int dma_buf_fd,
                                            size_t dma_buf_capacity,
                                            const uint8_t* jpeg,
                                            size_t jpeg_len,
                                            void** out_packet) {
    if (!out_packet || jpeg_len == 0) {
        return false;
    }
    MppBuffer mbuf = nullptr;
    const bool want_ext_import = (dma_buf_fd >= 0 && dma_buf_capacity >= jpeg_len && WantExtDmabufImport());
#if defined(RFLOW_HAVE_LIBRGA)
    const bool want_rga = (dma_buf_fd >= 0 && dma_buf_capacity >= jpeg_len && WantRgaToMpp() && !want_ext_import);
#else
    const bool want_rga = false;
#endif

    if (want_ext_import) {
        // Keep DMA visibility coherent before import on some BSPs.
        {
            struct dma_buf_sync sync {};
            sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ;
            if (ioctl(dma_buf_fd, DMA_BUF_IOCTL_SYNC, &sync) != 0) {
                if (MjpegDecTraceEnabled()) {
                    std::cerr << "[RkMppMjpeg] DMA_BUF_IOCTL_SYNC failed errno=" << errno << "\n";
                }
            }
        }
        MppBufferInfo info{};
        info.type = MPP_BUFFER_TYPE_EXT_DMA;
        info.size = dma_buf_capacity;
        info.fd = dma_buf_fd;
        info.ptr = nullptr;
        info.hnd = nullptr;
        info.index = 0;
        if (mpp_buffer_import(&mbuf, &info) != MPP_OK || !mbuf) {
            if (MjpegDecTraceEnabled()) {
                std::cerr << "[RkMppMjpeg] mpp_buffer_import EXT_DMA failed fd=" << dma_buf_fd << " cap=" << dma_buf_capacity
                          << "\n";
            }
            return false;
        }
    } else {
        if (!jpeg && !want_rga) {
            return false;
        }
        MppBufferGroup ig = reinterpret_cast<MppBufferGroup>(input_group_);
#if defined(RFLOW_HAVE_LIBRGA)
        bool try_rga = want_rga && !session_skip_rga_;
        size_t in_alloc = jpeg_len;
        if (try_rga) {
            const size_t rga_min_total = RgaY400SmallestTotalBytes(jpeg_len);
            if (dma_buf_capacity < rga_min_total) {
                try_rga = false;
            } else {
                in_alloc = std::max(jpeg_len, rga_min_total);
            }
        }
#else
        size_t in_alloc = jpeg_len;
#endif
        if (mpp_buffer_get(ig, &mbuf, in_alloc) != MPP_OK || !mbuf) {
            if (MjpegDecTraceEnabled()) {
                std::cerr << "[RkMppMjpeg] mpp_buffer_get input jpeg failed len=" << in_alloc << "\n";
            }
            return false;
        }
        void* dst = mpp_buffer_get_ptr(mbuf);
        if (!dst) {
            mpp_buffer_put(mbuf);
            return false;
        }
        bool filled = false;
#if defined(RFLOW_HAVE_LIBRGA)
        if (try_rga) {
            const int dst_fd = mpp_buffer_get_fd(mbuf);
            const size_t dst_cap = mpp_buffer_get_size(mbuf);
            const auto t_rga0 = std::chrono::steady_clock::now();
            const bool rga_ok =
                (dst_fd >= 0) && RgaCopyDmaBufJpeg(dma_buf_fd, dma_buf_capacity, dst_fd, dst_cap, jpeg_len);
            if (LatencyTraceEnabled()) {
                const double rga_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_rga0).count();
                std::cout << "[Latency] MPP JPEG input RGA copy ms=" << rga_ms << " ok=" << (rga_ok ? 1 : 0) << "\n";
            }
            if (rga_ok) {
                filled = true;
            } else {
                if (RgaDisableAfterFailEnabled()) {
                    session_skip_rga_ = true;
                    if (LatencyTraceEnabled() || MjpegDecTraceEnabled()) {
                        std::cerr << "[RkMppMjpeg] RGA disabled for rest of session "
                                     "(WEBRTC_MJPEG_RGA_DISABLE_AFTER_FAIL=1)\n";
                    }
                }
                if (MjpegDecTraceEnabled()) {
                    std::cerr << "[RkMppMjpeg] RGA copy failed, falling back to memcpy\n";
                }
            }
        }
#endif
        if (!filled) {
            if (!jpeg) {
                mpp_buffer_put(mbuf);
                return false;
            }
            const auto t_mc0 = std::chrono::steady_clock::now();
            std::memcpy(dst, jpeg, jpeg_len);
            if (LatencyTraceEnabled()) {
                const double mc_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_mc0).count();
                std::cout << "[Latency] MPP JPEG input memcpy ms=" << mc_ms << " bytes=" << jpeg_len << "\n";
            }
        }
    }
    mpp_buffer_set_index(mbuf, kMppBufferGroupIndex);

    MppPacket packet = nullptr;
    if (mpp_packet_init_with_buffer(&packet, mbuf) != MPP_OK || !packet) {
        mpp_buffer_put(mbuf);
        return false;
    }
    mpp_buffer_put(mbuf);
    mpp_packet_set_size(packet, jpeg_len);
    mpp_packet_set_length(packet, jpeg_len);
    *out_packet = packet;
    return true;
}

bool RkMppMjpegDecoder::SendMppPacket(void* packet_vp) {
    MppCtx ctx = reinterpret_cast<MppCtx>(ctx_);
    MppApi* mpi = reinterpret_cast<MppApi*>(mpi_);
    MppPacket packet = static_cast<MppPacket>(packet_vp);
    MppBufferGroup og = reinterpret_cast<MppBufferGroup>(output_buf_group_);

    const int timeout_ms = 200;
    const int interval_ms = MjpegDecLowLatency() ? 2 : 5;
    int wait_left = MjpegDecLowLatency() ? 800 : 2000;

    while (wait_left > 0) {
        mpi->poll(ctx, MPP_PORT_INPUT, static_cast<MppPollType>(interval_ms));
        MppTask task = nullptr;
        const MPP_RET dq = mpi->dequeue(ctx, MPP_PORT_INPUT, &task);
        if (dq < 0 || !task) {
            wait_left -= interval_ms;
            continue;
        }

        mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, packet);

        MppBuffer out_mbuf = nullptr;
        if (mpp_buffer_get(og, &out_mbuf, output_buf_size_) != MPP_OK || !out_mbuf) {
            if (MjpegDecTraceEnabled()) {
                std::cerr << "[RkMppMjpeg] mpp_buffer_get output failed size=" << output_buf_size_ << "\n";
            }
            mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, nullptr);
            mpi->enqueue(ctx, MPP_PORT_INPUT, task);
            return false;
        }
        mpp_buffer_set_index(out_mbuf, kMppBufferGroupIndex);

        MppFrame mframe = nullptr;
        mpp_frame_init(&mframe);
        mpp_frame_set_buffer(mframe, out_mbuf);
        mpp_buffer_put(out_mbuf);

        MppMeta meta = mpp_frame_get_meta(mframe);
        mpp_meta_set_packet(meta, KEY_INPUT_PACKET, packet);

        mpp_task_meta_set_frame(task, KEY_OUTPUT_FRAME, mframe);

        if (mpi->enqueue(ctx, MPP_PORT_INPUT, task) < 0) {
            mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, nullptr);
            mpp_task_meta_set_frame(task, KEY_OUTPUT_FRAME, nullptr);
            mpi->enqueue(ctx, MPP_PORT_INPUT, task);
            mpp_frame_deinit(&mframe);
            return false;
        }
        return true;
    }
    if (MjpegDecTraceEnabled()) {
        std::cerr << "[RkMppMjpeg] SendMppPacket: dequeue input task timeout\n";
    }
    return false;
}

void* RkMppMjpegDecoder::PollMppFrame(int timeout_ms) {
    MppCtx ctx = reinterpret_cast<MppCtx>(ctx_);
    MppApi* mpi = reinterpret_cast<MppApi*>(mpi_);

    // Same as gstmppjpegdec poll_mpp_frame: non-zero means not-ready/timeout.
    if (mpi->poll(ctx, MPP_PORT_OUTPUT, static_cast<MppPollType>(timeout_ms))) {
        return nullptr;
    }
    MppTask task = nullptr;
    if (mpi->dequeue(ctx, MPP_PORT_OUTPUT, &task) < 0 || !task) {
        return nullptr;
    }
    MppFrame mframe = nullptr;
    mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &mframe);
    if (!mframe) {
        mpi->enqueue(ctx, MPP_PORT_OUTPUT, task);
        return nullptr;
    }
    MppMeta meta = mpp_frame_get_meta(mframe);
    MppPacket mpkt = nullptr;
    mpp_meta_get_packet(meta, KEY_INPUT_PACKET, &mpkt);
    if (mpkt) {
        mpp_packet_deinit(&mpkt);
    }
    mpi->enqueue(ctx, MPP_PORT_OUTPUT, task);
    return mframe;
}

bool RkMppMjpegDecoder::HandleInfoChangeFrame(void* frame_vp) {
    MppFrame frame = static_cast<MppFrame>(frame_vp);
    MppCtx ctx = reinterpret_cast<MppCtx>(ctx_);
    MppApi* mpi = reinterpret_cast<MppApi*>(mpi_);

    const RK_U32 bs = mpp_frame_get_buf_size(frame);
    if (bs > 0 && static_cast<size_t>(bs) > output_buf_size_) {
        output_buf_size_ = bs;
        if (MjpegDecTraceEnabled()) {
            std::cerr << "[RkMppMjpeg] info_change: bump output_buf_size to " << output_buf_size_ << "\n";
        }
    }

    if (mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr) != MPP_OK) {
        std::cerr << "[RkMppMjpeg] MPP_DEC_SET_INFO_CHANGE_READY failed\n";
        return false;
    }
    return true;
}

bool RkMppMjpegDecoder::DecodeJpegToI420(const uint8_t* jpeg,
                                         size_t jpeg_len,
                                         int expect_w,
                                         int expect_h,
                                         webrtc::I420Buffer* out_i420,
                                         int dma_buf_fd,
                                         size_t dma_buf_capacity) {
    if (!ctx_ || !mpi_ || jpeg_len == 0 || !out_i420) {
        return false;
    }
    if (dma_buf_fd < 0 && !jpeg) {
        return false;
    }
    if (expect_w <= 0 || expect_h <= 0) {
        return false;
    }
    if (expect_w != last_expect_w_ || expect_h != last_expect_h_) {
        last_expect_w_ = expect_w;
        last_expect_h_ = expect_h;
        output_buf_size_ = ComputeJpegOutputBufSize(expect_w, expect_h);
    }
    if (output_buf_size_ == 0) {
        return false;
    }
    bool decoded = false;
    constexpr int kMaxSubmit = 8;
    constexpr int kMaxPollPerSubmit = 500;

    for (int submit = 0; submit < kMaxSubmit && !decoded; ++submit) {
        MppPacket packet = nullptr;
        if (!BuildMppInputPacket(dma_buf_fd, dma_buf_capacity, jpeg, jpeg_len, reinterpret_cast<void**>(&packet))) {
            return false;
        }
        if (!SendMppPacket(packet)) {
            mpp_packet_deinit(&packet);
            if (MjpegDecTraceEnabled()) {
                std::cerr << "[RkMppMjpeg] SendMppPacket failed submit=" << submit << "\n";
            }
            return false;
        }

        bool need_resubmit = false;
        for (int poll_i = 0; poll_i < kMaxPollPerSubmit; ++poll_i) {
            MppFrame frame = static_cast<MppFrame>(PollMppFrame(MjpegPollOutputTimeoutMs()));
            if (!frame) {
                usleep(static_cast<unsigned>(MjpegPollSleepUs()));
                continue;
            }
            if (mpp_frame_get_info_change(frame)) {
                if (!HandleInfoChangeFrame(frame)) {
                    mpp_frame_deinit(&frame);
                    return false;
                }
                mpp_frame_deinit(&frame);
                need_resubmit = true;
                break;
            }

            const RK_U32 err = mpp_frame_get_errinfo(frame);
            const RK_U32 discard = mpp_frame_get_discard(frame);
            if (err || discard) {
                if (MjpegDecTraceEnabled()) {
                    std::cerr << "[RkMppMjpeg] frame errinfo=" << err << " discard=" << discard << "\n";
                }
                mpp_frame_deinit(&frame);
                return false;
            }

            const int fw = static_cast<int>(mpp_frame_get_width(frame));
            const int fh = static_cast<int>(mpp_frame_get_height(frame));
            if (fw != expect_w || fh != expect_h) {
                if (MjpegDecTraceEnabled()) {
                    std::cerr << "[RkMppMjpeg] size mismatch decoded " << fw << "x" << fh << " expect " << expect_w << "x"
                              << expect_h << "\n";
                }
                mpp_frame_deinit(&frame);
                return false;
            }

            MppBuffer mbuf = mpp_frame_get_buffer(frame);
            if (!mbuf) {
                mpp_frame_deinit(&frame);
                return false;
            }
            auto* yuv = static_cast<const uint8_t*>(mpp_buffer_get_ptr(mbuf));
            if (!yuv) {
                mpp_frame_deinit(&frame);
                return false;
            }

            const RK_U32 fmt = mpp_frame_get_fmt(frame);
            const int hs = static_cast<int>(mpp_frame_get_hor_stride(frame));
            const int ver_stride = static_cast<int>(mpp_frame_get_ver_stride(frame));
            const uint8_t* src_y = yuv;
            const uint8_t* src_uv = yuv + static_cast<size_t>(hs) * static_cast<size_t>(ver_stride);

            int conv = 0;
            if (fmt == MPP_FMT_YUV420SP) {
                conv = libyuv::NV12ToI420(src_y, hs, src_uv, hs, out_i420->MutableDataY(), out_i420->StrideY(),
                                          out_i420->MutableDataU(), out_i420->StrideU(), out_i420->MutableDataV(),
                                          out_i420->StrideV(), fw, fh);
            } else if (fmt == MPP_FMT_YUV420SP_VU) {
                conv = libyuv::NV21ToI420(src_y, hs, src_uv, hs, out_i420->MutableDataY(), out_i420->StrideY(),
                                          out_i420->MutableDataU(), out_i420->StrideU(), out_i420->MutableDataV(),
                                          out_i420->StrideV(), fw, fh);
            } else {
                if (MjpegDecTraceEnabled()) {
                    std::cerr << "[RkMppMjpeg] unexpected fmt=" << fmt << "\n";
                }
                mpp_frame_deinit(&frame);
                return false;
            }

            mpp_frame_deinit(&frame);
            decoded = (conv == 0);
            if (!decoded && MjpegDecTraceEnabled()) {
                std::cerr << "[RkMppMjpeg] libyuv convert failed conv=" << conv << "\n";
            }
            break;
        }

        if (!decoded && !need_resubmit) {
            if (MjpegDecTraceEnabled()) {
                std::cerr << "[RkMppMjpeg] decode timeout (no output frame) submit=" << submit << "\n";
            }
            return false;
        }
    }

    if (!decoded && MjpegDecTraceEnabled()) {
        std::cerr << "[RkMppMjpeg] DecodeJpegToI420 failed after resubmit loop\n";
    }
    return decoded;
}

bool RkMppMjpegDecoder::DecodeJpegToNV12(const uint8_t* jpeg,
                                         size_t jpeg_len,
                                         int expect_w,
                                         int expect_h,
                                         webrtc::NV12Buffer* out_nv12,
                                         int dma_buf_fd,
                                         size_t dma_buf_capacity) {
    if (!ctx_ || !mpi_ || jpeg_len == 0 || !out_nv12) {
        return false;
    }
    if (dma_buf_fd < 0 && !jpeg) {
        return false;
    }
    if (expect_w <= 0 || expect_h <= 0) {
        return false;
    }
    if (out_nv12->width() != expect_w || out_nv12->height() != expect_h) {
        return false;
    }
    if (expect_w != last_expect_w_ || expect_h != last_expect_h_) {
        last_expect_w_ = expect_w;
        last_expect_h_ = expect_h;
        output_buf_size_ = ComputeJpegOutputBufSize(expect_w, expect_h);
    }
    if (output_buf_size_ == 0) {
        return false;
    }
    bool decoded = false;
    constexpr int kMaxSubmit = 8;
    constexpr int kMaxPollPerSubmit = 500;

    for (int submit = 0; submit < kMaxSubmit && !decoded; ++submit) {
        MppPacket packet = nullptr;
        if (!BuildMppInputPacket(dma_buf_fd, dma_buf_capacity, jpeg, jpeg_len, reinterpret_cast<void**>(&packet))) {
            return false;
        }
        if (!SendMppPacket(packet)) {
            mpp_packet_deinit(&packet);
            if (MjpegDecTraceEnabled()) {
                std::cerr << "[RkMppMjpeg] SendMppPacket failed submit=" << submit << "\n";
            }
            return false;
        }

        bool need_resubmit = false;
        for (int poll_i = 0; poll_i < kMaxPollPerSubmit; ++poll_i) {
            MppFrame frame = static_cast<MppFrame>(PollMppFrame(MjpegPollOutputTimeoutMs()));
            if (!frame) {
                usleep(static_cast<unsigned>(MjpegPollSleepUs()));
                continue;
            }
            if (mpp_frame_get_info_change(frame)) {
                if (!HandleInfoChangeFrame(frame)) {
                    mpp_frame_deinit(&frame);
                    return false;
                }
                mpp_frame_deinit(&frame);
                need_resubmit = true;
                break;
            }

            const RK_U32 err = mpp_frame_get_errinfo(frame);
            const RK_U32 discard = mpp_frame_get_discard(frame);
            if (err || discard) {
                if (MjpegDecTraceEnabled()) {
                    std::cerr << "[RkMppMjpeg] frame errinfo=" << err << " discard=" << discard << "\n";
                }
                mpp_frame_deinit(&frame);
                return false;
            }

            const int fw = static_cast<int>(mpp_frame_get_width(frame));
            const int fh = static_cast<int>(mpp_frame_get_height(frame));
            if (fw != expect_w || fh != expect_h) {
                if (MjpegDecTraceEnabled()) {
                    std::cerr << "[RkMppMjpeg] size mismatch decoded " << fw << "x" << fh << " expect " << expect_w << "x"
                              << expect_h << "\n";
                }
                mpp_frame_deinit(&frame);
                return false;
            }

            MppBuffer mbuf = mpp_frame_get_buffer(frame);
            if (!mbuf) {
                mpp_frame_deinit(&frame);
                return false;
            }
            auto* yuv = static_cast<const uint8_t*>(mpp_buffer_get_ptr(mbuf));
            if (!yuv) {
                mpp_frame_deinit(&frame);
                return false;
            }

            const RK_U32 fmt = mpp_frame_get_fmt(frame);
            const int hs = static_cast<int>(mpp_frame_get_hor_stride(frame));
            const int ver_stride = static_cast<int>(mpp_frame_get_ver_stride(frame));
            const uint8_t* src_y = yuv;
            const uint8_t* src_uv = yuv + static_cast<size_t>(hs) * static_cast<size_t>(ver_stride);

            decoded = CopyMppSemiPlanarToNv12(fmt, src_y, src_uv, hs, fw, fh, out_nv12);
            mpp_frame_deinit(&frame);
            if (!decoded && MjpegDecTraceEnabled()) {
                std::cerr << "[RkMppMjpeg] CopyMppSemiPlanarToNv12 failed fmt=" << fmt << "\n";
            }
            break;
        }

        if (!decoded && !need_resubmit) {
            if (MjpegDecTraceEnabled()) {
                std::cerr << "[RkMppMjpeg] decode timeout (no output frame) submit=" << submit << "\n";
            }
            return false;
        }
    }

    if (!decoded && MjpegDecTraceEnabled()) {
        std::cerr << "[RkMppMjpeg] DecodeJpegToNV12 failed after resubmit loop\n";
    }
    return decoded;
}

bool RkMppMjpegDecoder::DecodeJpegToNativeDecFrame(const uint8_t* jpeg,
                                                  size_t jpeg_len,
                                                  int expect_w,
                                                  int expect_h,
                                                  webrtc::scoped_refptr<MppNativeDecFrameBuffer>* out,
                                                  int dma_buf_fd,
                                                  size_t dma_buf_capacity,
                                                  int64_t dq_time_us,
                                                  int64_t v4l2_timestamp_us,
                                                  int64_t poll_wait_us,
                                                  int64_t dqbuf_ioctl_us,
                                                  int64_t decode_queue_wait_us) {
    if (!out) {
        return false;
    }
    *out = nullptr;
    if (!ctx_ || !mpi_ || jpeg_len == 0) {
        return false;
    }
    if (dma_buf_fd < 0 && !jpeg) {
        return false;
    }
    if (expect_w <= 0 || expect_h <= 0) {
        return false;
    }
    if (expect_w != last_expect_w_ || expect_h != last_expect_h_) {
        last_expect_w_ = expect_w;
        last_expect_h_ = expect_h;
        output_buf_size_ = ComputeJpegOutputBufSize(expect_w, expect_h);
    }
    if (output_buf_size_ == 0) {
        return false;
    }

    const int64_t mjpeg_input_us = webrtc::TimeMicros();
    const int64_t wall_capture_utc_ms = webrtc::TimeUTCMillis();
    bool decoded = false;
    constexpr int kMaxSubmit = 8;
    constexpr int kMaxPollPerSubmit = 500;

    for (int submit = 0; submit < kMaxSubmit && !decoded; ++submit) {
        MppPacket packet = nullptr;
        if (!BuildMppInputPacket(dma_buf_fd, dma_buf_capacity, jpeg, jpeg_len, reinterpret_cast<void**>(&packet))) {
            return false;
        }
        if (!SendMppPacket(packet)) {
            mpp_packet_deinit(&packet);
            if (MjpegDecTraceEnabled()) {
                std::cerr << "[RkMppMjpeg] NativeDec: SendMppPacket failed submit=" << submit << "\n";
            }
            return false;
        }

        bool need_resubmit = false;
        for (int poll_i = 0; poll_i < kMaxPollPerSubmit; ++poll_i) {
            MppFrame frame = static_cast<MppFrame>(PollMppFrame(MjpegPollOutputTimeoutMs()));
            if (!frame) {
                usleep(static_cast<unsigned>(MjpegPollSleepUs()));
                continue;
            }
            if (mpp_frame_get_info_change(frame)) {
                if (!HandleInfoChangeFrame(frame)) {
                    mpp_frame_deinit(&frame);
                    return false;
                }
                mpp_frame_deinit(&frame);
                need_resubmit = true;
                break;
            }

            const RK_U32 err = mpp_frame_get_errinfo(frame);
            const RK_U32 discard = mpp_frame_get_discard(frame);
            if (err || discard) {
                if (MjpegDecTraceEnabled()) {
                    std::cerr << "[RkMppMjpeg] NativeDec: errinfo=" << err << " discard=" << discard << "\n";
                }
                mpp_frame_deinit(&frame);
                return false;
            }

            const int fw = static_cast<int>(mpp_frame_get_width(frame));
            const int fh = static_cast<int>(mpp_frame_get_height(frame));
            if (fw != expect_w || fh != expect_h) {
                if (MjpegDecTraceEnabled()) {
                    std::cerr << "[RkMppMjpeg] NativeDec: size mismatch decoded " << fw << "x" << fh << " expect "
                              << expect_w << "x" << expect_h << "\n";
                }
                mpp_frame_deinit(&frame);
                return false;
            }

            MppBuffer mbuf = mpp_frame_get_buffer(frame);
            if (!mbuf) {
                mpp_frame_deinit(&frame);
                return false;
            }
            if (!mpp_buffer_get_ptr(mbuf)) {
                mpp_frame_deinit(&frame);
                return false;
            }

            const RK_U32 fmt = mpp_frame_get_fmt(frame);
            const int hs = static_cast<int>(mpp_frame_get_hor_stride(frame));
            const int ver_stride = static_cast<int>(mpp_frame_get_ver_stride(frame));

            webrtc::scoped_refptr<MppNativeDecFrameBuffer> wrapped =
                MppNativeDecFrameBuffer::CreateFromMppFrame(frame, fw, fh, hs, ver_stride, fmt, mjpeg_input_us,
                                                            dq_time_us, v4l2_timestamp_us, poll_wait_us,
                                                            dqbuf_ioctl_us, decode_queue_wait_us, wall_capture_utc_ms);
            if (!wrapped) {
                mpp_frame_deinit(&frame);
                return false;
            }
            *out = wrapped;
            decoded = true;
            break;
        }

        if (!decoded && !need_resubmit) {
            if (MjpegDecTraceEnabled()) {
                std::cerr << "[RkMppMjpeg] NativeDec: decode timeout submit=" << submit << "\n";
            }
            return false;
        }
    }

    if (!decoded && MjpegDecTraceEnabled()) {
        std::cerr << "[RkMppMjpeg] DecodeJpegToNativeDecFrame failed after resubmit loop\n";
    }
    return decoded;
}

}  // namespace rflow::rtc::hw::rockchip_mpp

/**
 * @file  android_mediacodec_video_decoder.cpp
 * @brief Android AMediaCodec H.264 解码器实现（从旧工程移植）。
 *
 * 迁移改动：
 *   - 命名空间：webrtc_demo -> robrt::client::impl；
 *   - 日志：__android_log_print / ALOG* -> ROBRT_LOGW / ROBRT_LOGI；
 *   - 移除对 encoded_tracking_bridge.h / video_decode_sink_timing_bridge.h 的依赖
 *     （旧工程的 E2E 追踪桥接暂未移植）。TODO: 接入 SDK 的 stream_stats / trace 能力。
 */

#include "android_mediacodec_video_decoder.h"

#include "common/internal/logger.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_type.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/ref_counter.h"
#include "libyuv/convert.h"

// NDK r26+ 的 NdkMediaCodec.h 可能不再定义 KEY_FRAME；与 Java MediaCodec.BUFFER_FLAG_KEY_FRAME 一致。
#ifndef AMEDIACODEC_BUFFER_FLAG_KEY_FRAME
#define AMEDIACODEC_BUFFER_FLAG_KEY_FRAME 1u
#endif

namespace robrt::client::impl {

namespace {

// COLOR_FormatYUV420SemiPlanar
constexpr int32_t kColorFormatNv12           = 21;
// COLOR_FormatYUV420Flexible
constexpr int32_t kColorFormatYuv420Flexible = 0x7F420888;
// 部分厂商 Codec2 在 AMessage 里使用与 color-format 不同的 android._color-format
constexpr int32_t kColorFormatQtiSurface     = 2141391876;

// 与 Java MediaFormat.KEY_LOW_LATENCY 一致；部分设备在 API 30+ 上可降低解码器内部排队。
constexpr char kMediaFormatLowLatency[] = "low-latency";

// queue 后 drain：先非阻塞清空已就绪帧，再单次短阻塞吸收「刚完成」的 output。
constexpr int64_t kDrainAfterQueueShortWaitUs = 3000;
constexpr int64_t kDrainOnInputBackpressureUs = 3000;
constexpr int64_t kDequeueInputTimeoutUs      = 3000;

int64_t McMonotonicUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool IsNv12FamilyOutput(int32_t fmt) {
    if (fmt == 0) return true;
    return fmt == kColorFormatNv12 || fmt == kColorFormatYuv420Flexible ||
           fmt == kColorFormatQtiSurface;
}

// WebRTC H264 接收路径多为 Annex B（00 00 01 / 00 00 00 01）。Codec2 解码器通常需要 Annex B；
// 若误转成 AVCC（4 字节长度前缀），部分机型会吃满 input 但永远不出 output（fps=0）。
bool LooksLikeAnnexB(const uint8_t* d, size_t sz) {
    if (sz < 4 || !d) return false;
    if (d[0] == 0 && d[1] == 0 && d[2] == 1) return true;
    if (d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 1) return true;
    return false;
}

std::vector<std::pair<const uint8_t*, size_t>> SplitAnnexB(const uint8_t* data, size_t size) {
    std::vector<std::pair<const uint8_t*, size_t>> nals;
    if (!data || size < 3) return nals;
    size_t i = 0;
    while (i < size) {
        size_t nal_start = 0;
        if (i + 3 <= size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            nal_start = i + 3;
            i += 3;
        } else if (i + 4 <= size && data[i] == 0 && data[i + 1] == 0 &&
                   data[i + 2] == 0 && data[i + 3] == 1) {
            nal_start = i + 4;
            i += 4;
        } else {
            ++i;
            continue;
        }
        size_t j = nal_start;
        while (j < size) {
            if (j + 3 <= size && data[j] == 0 && data[j + 1] == 0 &&
                (data[j + 2] == 1 || (j + 4 <= size && data[j + 2] == 0 && data[j + 3] == 1))) {
                break;
            }
            ++j;
        }
        if (j > nal_start) nals.push_back({data + nal_start, j - nal_start});
        i = j;
    }
    return nals;
}

void AnnexBToAvcc(const uint8_t* data, size_t size, std::vector<uint8_t>* out) {
    out->clear();
    if (!data || size == 0) return;
    const auto nals = SplitAnnexB(data, size);
    if (nals.empty()) {
        out->push_back(static_cast<uint8_t>((size >> 24) & 0xff));
        out->push_back(static_cast<uint8_t>((size >> 16) & 0xff));
        out->push_back(static_cast<uint8_t>((size >> 8) & 0xff));
        out->push_back(static_cast<uint8_t>(size & 0xff));
        out->insert(out->end(), data, data + size);
        return;
    }
    for (const auto& nal : nals) {
        const size_t len = nal.second;
        if (len == 0) continue;
        out->push_back(static_cast<uint8_t>((len >> 24) & 0xff));
        out->push_back(static_cast<uint8_t>((len >> 16) & 0xff));
        out->push_back(static_cast<uint8_t>((len >> 8) & 0xff));
        out->push_back(static_cast<uint8_t>(len & 0xff));
        out->insert(out->end(), nal.first, nal.first + len);
    }
}

// NV12→I420 直接写入已分配的 dst 平面。
bool FillI420FromNv12(const uint8_t* src, size_t src_cap, int offset,
                      int width, int height, int y_stride, int slice_height,
                      uint8_t* dy, int dsy, uint8_t* du, int dsu, uint8_t* dv, int dsv) {
    if (width <= 0 || height <= 0 || y_stride < width) return false;
    const int y_plane = y_stride * slice_height;
    if (offset + y_plane + y_stride * (slice_height / 2) > static_cast<int>(src_cap)) return false;
    const uint8_t* ys = src + offset;
    return libyuv::NV12ToI420(ys, y_stride, ys + y_plane, y_stride,
                              dy, dsy, du, dsu, dv, dsv, width, height) == 0;
}

bool FillI420FromNv12Tight(const uint8_t* src, size_t src_cap, int offset,
                           int width, int height,
                           uint8_t* dy, int dsy, uint8_t* du, int dsu, uint8_t* dv, int dsv) {
    if (width <= 0 || height <= 0) return false;
    const int need = width * height + width * (height / 2);
    if (offset < 0 || offset + need > static_cast<int>(src_cap)) return false;
    const uint8_t* ys = src + offset;
    return libyuv::NV12ToI420(ys, width, ys + width * height, width,
                              dy, dsy, du, dsu, dv, dsv, width, height) == 0;
}

// -------------------------------------------------------------------------
// I420 内存池：避免每帧 mmap + page-fault；槽位内存与 PooledI420 共享生命周期。
// -------------------------------------------------------------------------
struct I420PoolSlot {
    std::shared_ptr<uint8_t> mem;
    int                      width = 0, height = 0;
    int                      stride_y = 0, stride_u = 0, stride_v = 0;
    size_t                   off_u = 0, off_v = 0;
    std::atomic<bool>        free{true};
};

class PooledI420 final : public webrtc::I420BufferInterface {
 public:
    PooledI420(std::shared_ptr<uint8_t> m, int w, int h,
               int sy, int su, int sv, size_t ou, size_t ov,
               std::atomic<bool>* flag)
        : m_(std::move(m)), w_(w), h_(h),
          sy_(sy), su_(su), sv_(sv), ou_(ou), ov_(ov), flag_(flag) {}
    ~PooledI420() override {
        if (flag_) flag_->store(true, std::memory_order_release);
    }

    void AddRef() const override { rc_.IncRef(); }
    webrtc::RefCountReleaseStatus Release() const override {
        auto s = rc_.DecRef();
        if (s == webrtc::RefCountReleaseStatus::kDroppedLastRef) delete this;
        return s;
    }

    int width()  const override { return w_; }
    int height() const override { return h_; }
    const uint8_t* DataY() const override { return m_.get(); }
    const uint8_t* DataU() const override { return m_.get() + ou_; }
    const uint8_t* DataV() const override { return m_.get() + ov_; }
    int StrideY() const override { return sy_; }
    int StrideU() const override { return su_; }
    int StrideV() const override { return sv_; }

 private:
    std::shared_ptr<uint8_t>                 m_;
    int                                      w_, h_, sy_, su_, sv_;
    size_t                                   ou_, ov_;
    std::atomic<bool>*                       flag_;
    mutable webrtc::webrtc_impl::RefCounter  rc_{0};
};

}  // namespace

struct AndroidMediaCodecVideoDecoder::Impl {
    std::mutex                        mu_;
    std::condition_variable           cv_;
    std::deque<std::function<void()>> tasks_;
    bool                              running_ = false;
    std::thread                       thread_;

    AMediaCodec*                  codec_    = nullptr;
    webrtc::DecodedImageCallback* callback_ = nullptr;

    int     out_width_     = 0;
    int     out_height_    = 0;
    int     y_stride_      = 0;
    int     slice_height_  = 0;
    int32_t color_format_  = 0;

    std::vector<uint8_t> avcc_scratch_;

    // 输入 PTS 使用单调递增时间戳；RTP 时间戳会回绕/乱序，易让 Codec2 PipelineWatcher 产生噪声告警。
    int64_t next_input_pts_us_ = 0;

    static constexpr int kPoolSlots = 6;
    I420PoolSlot pool_[kPoolSlots];

    struct PoolResult {
        uint8_t *y, *u, *v;
        int     sy, su, sv;
        size_t  ou, ov;
        std::shared_ptr<uint8_t> mem;
        std::atomic<bool>*       flag;
    };

    bool AcquirePoolSlot(int w, int h, PoolResult* r) {
        const int    sy = w, su = (w + 1) / 2, sv = su;
        const int    hh = (h + 1) / 2;
        const size_t ou = static_cast<size_t>(sy) * h;
        const size_t ov = ou + static_cast<size_t>(su) * hh;
        const size_t total = ov + static_cast<size_t>(sv) * hh;
        for (auto& s : pool_) {
            if (!s.free.load(std::memory_order_acquire)) continue;
            if (s.width == w && s.height == h && s.mem) {
                s.free.store(false, std::memory_order_relaxed);
                r->y = s.mem.get(); r->u = r->y + s.off_u; r->v = r->y + s.off_v;
                r->sy = s.stride_y; r->su = s.stride_u; r->sv = s.stride_v;
                r->ou = s.off_u;    r->ov = s.off_v;
                r->mem = s.mem;     r->flag = &s.free;
                return true;
            }
        }
        for (auto& s : pool_) {
            if (!s.free.load(std::memory_order_acquire)) continue;
            s.mem.reset(new uint8_t[total], std::default_delete<uint8_t[]>());
            s.width = w; s.height = h;
            s.stride_y = sy; s.stride_u = su; s.stride_v = sv;
            s.off_u = ou; s.off_v = ov;
            s.free.store(false, std::memory_order_relaxed);
            r->y = s.mem.get(); r->u = r->y + ou; r->v = r->y + ov;
            r->sy = sy; r->su = su; r->sv = sv;
            r->ou = ou; r->ov = ov;
            r->mem = s.mem; r->flag = &s.free;
            return true;
        }
        return false;
    }

    void ClearPool() {
        for (auto& s : pool_) {
            s.mem.reset();
            s.width = s.height = 0;
            s.free.store(true, std::memory_order_relaxed);
        }
    }

    void WorkerLoop() {
        std::unique_lock<std::mutex> lk(mu_);
        while (running_) {
            cv_.wait(lk, [this] { return !tasks_.empty() || !running_; });
            if (!running_) break;
            while (!tasks_.empty()) {
                auto job = std::move(tasks_.front());
                tasks_.pop_front();
                lk.unlock();
                if (job) job();
                lk.lock();
            }
        }
    }

    void StopWorker() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            running_ = false;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    void DestroyCodec() {
        if (codec_) {
            AMediaCodec_stop(codec_);
            AMediaCodec_delete(codec_);
            codec_ = nullptr;
        }
        out_width_ = out_height_ = y_stride_ = slice_height_ = 0;
        color_format_ = 0;
        ClearPool();
    }

    void UpdateOutputFormat(AMediaFormat* fmt) {
        if (!fmt) return;
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH,  &out_width_);
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &out_height_);
        if (!AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, &color_format_)) {
            int32_t alt = 0;
            if (AMediaFormat_getInt32(fmt, "android._color-format", &alt)) color_format_ = alt;
        }
        if (!AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_STRIDE, &y_stride_) ||
            y_stride_ < out_width_) {
            y_stride_ = out_width_;
        }
        if (!AMediaFormat_getInt32(fmt, "slice-height", &slice_height_) ||
            slice_height_ < out_height_) {
            slice_height_ = out_height_;
        }
    }

    void RefreshOutputFormat() {
        if (!codec_) return;
        AMediaFormat* fmt = AMediaCodec_getOutputFormat(codec_);
        if (fmt) {
            UpdateOutputFormat(fmt);
            AMediaFormat_delete(fmt);
        }
    }

    bool ConfigureOnWorker(const webrtc::VideoDecoder::Settings& settings) {
        DestroyCodec();
        int w = 1920;
        int h = 1080;
        if (settings.max_render_resolution().Valid()) {
            w = settings.max_render_resolution().Width();
            h = settings.max_render_resolution().Height();
        }
        codec_ = AMediaCodec_createDecoderByType("video/avc");
        if (!codec_) {
            ROBRT_LOGW("[mc_dec] AMediaCodec_createDecoderByType failed");
            return false;
        }
        AMediaFormat* format = AMediaFormat_new();
        AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME,   "video/avc");
        AMediaFormat_setInt32 (format, AMEDIAFORMAT_KEY_WIDTH,  w);
        AMediaFormat_setInt32 (format, AMEDIAFORMAT_KEY_HEIGHT, h);
#if __ANDROID_API__ >= 30
        AMediaFormat_setInt32(format, kMediaFormatLowLatency, 1);
#endif
        media_status_t st = AMediaCodec_configure(codec_, format, nullptr, nullptr, 0);
        AMediaFormat_delete(format);
        if (st != AMEDIA_OK) {
            ROBRT_LOGW("[mc_dec] AMediaCodec_configure failed: %d", static_cast<int>(st));
            AMediaCodec_delete(codec_);
            codec_ = nullptr;
            return false;
        }
        st = AMediaCodec_start(codec_);
        if (st != AMEDIA_OK) {
            ROBRT_LOGW("[mc_dec] AMediaCodec_start failed: %d", static_cast<int>(st));
            AMediaCodec_delete(codec_);
            codec_ = nullptr;
            return false;
        }
        next_input_pts_us_ = 0;
        RefreshOutputFormat();
        return true;
    }

    void DrainOutputs(int64_t render_time_ms,
                      uint32_t rtp_timestamp,
                      int64_t first_dequeue_timeout_us,
                      int* delivered_frames,
                      const std::optional<uint16_t>& video_frame_tracking_id) {
        if (!codec_) return;

        webrtc::DecodedImageCallback* cb = nullptr;
        {
            std::lock_guard<std::mutex> lk(mu_);
            cb = callback_;
        }
        // 无论是否有 callback，都必须 dequeue + release output，否则会塞满管道。

        bool used_timeout = false;
        for (;;) {
            AMediaCodecBufferInfo info;
            const int64_t t_us = (!used_timeout) ? first_dequeue_timeout_us : 0;
            used_timeout = true;
            ssize_t out_idx = AMediaCodec_dequeueOutputBuffer(codec_, &info, t_us);
            if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED ||
                out_idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
                RefreshOutputFormat();
                continue;
            }
            if (out_idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) break;
            if (out_idx < 0) break;

            if (info.size > 0 && (out_width_ <= 0 || out_height_ <= 0)) {
                RefreshOutputFormat();
            }

            if (info.size > 0 && out_width_ > 0 && out_height_ > 0 && cb &&
                IsNv12FamilyOutput(color_format_)) {
                size_t cap = 0;
                uint8_t* out_buf = AMediaCodec_getOutputBuffer(codec_,
                                                                static_cast<size_t>(out_idx),
                                                                &cap);
                if (out_buf &&
                    static_cast<size_t>(info.offset) + static_cast<size_t>(info.size) <= cap) {
                    webrtc::scoped_refptr<webrtc::I420BufferInterface> i420;
                    PoolResult pr{};
                    if (AcquirePoolSlot(out_width_, out_height_, &pr)) {
                        const bool ok =
                            FillI420FromNv12(out_buf, cap, info.offset, out_width_, out_height_,
                                             y_stride_, slice_height_,
                                             pr.y, pr.sy, pr.u, pr.su, pr.v, pr.sv) ||
                            FillI420FromNv12Tight(out_buf, cap, info.offset,
                                                  out_width_, out_height_,
                                                  pr.y, pr.sy, pr.u, pr.su, pr.v, pr.sv);
                        if (ok) {
                            i420 = webrtc::scoped_refptr<PooledI420>(
                                new PooledI420(pr.mem, out_width_, out_height_,
                                               pr.sy, pr.su, pr.sv, pr.ou, pr.ov, pr.flag));
                        } else {
                            pr.flag->store(true, std::memory_order_release);
                        }
                    }
                    if (!i420) {
                        auto buf420 = webrtc::I420Buffer::Create(out_width_, out_height_);
                        if (buf420) {
                            const bool ok =
                                FillI420FromNv12(out_buf, cap, info.offset, out_width_, out_height_,
                                                 y_stride_, slice_height_,
                                                 buf420->MutableDataY(), buf420->StrideY(),
                                                 buf420->MutableDataU(), buf420->StrideU(),
                                                 buf420->MutableDataV(), buf420->StrideV()) ||
                                FillI420FromNv12Tight(out_buf, cap, info.offset,
                                                      out_width_, out_height_,
                                                      buf420->MutableDataY(), buf420->StrideY(),
                                                      buf420->MutableDataU(), buf420->StrideU(),
                                                      buf420->MutableDataV(), buf420->StrideV());
                            if (ok) i420 = buf420;
                        }
                    }
                    if (i420) {
                        webrtc::VideoFrame::Builder fb;
                        fb.set_video_frame_buffer(i420)
                          .set_rtp_timestamp(rtp_timestamp)
                          .set_timestamp_us(render_time_ms * 1000);
                        if (video_frame_tracking_id.has_value()) {
                            fb.set_id(*video_frame_tracking_id);
                        }
                        webrtc::VideoFrame frame = fb.build();
                        cb->Decoded(frame);
                        if (delivered_frames) ++(*delivered_frames);
                        // TODO: E2E 耗时追踪已随 encoded_tracking_bridge 移除，后续由 SDK 统一 stats 实现。
                    } else {
                        ROBRT_LOGW("[mc_dec] NV12->I420 failed w=%d h=%d stride=%d slice=%d color=%d",
                                   out_width_, out_height_, y_stride_, slice_height_,
                                   static_cast<int>(color_format_));
                    }
                }
            } else if (info.size > 0 && out_width_ > 0 && out_height_ > 0 && cb) {
                ROBRT_LOGW("[mc_dec] unsupported color format %d",
                           static_cast<int>(color_format_));
            }

            AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(out_idx), false);
        }
    }

    void ProcessOneFrame(const std::vector<uint8_t>& data,
                         int64_t render_time_ms,
                         uint32_t rtp_timestamp,
                         bool is_keyframe,
                         const std::optional<uint16_t>& video_frame_tracking_id) {
        if (!codec_ || data.empty()) return;

        const uint8_t* feed_ptr = data.data();
        size_t         feed_sz  = data.size();
        if (!LooksLikeAnnexB(data.data(), data.size())) {
            AnnexBToAvcc(data.data(), data.size(), &avcc_scratch_);
            if (avcc_scratch_.empty()) return;
            feed_ptr = avcc_scratch_.data();
            feed_sz  = avcc_scratch_.size();
        }

        ssize_t in_idx = AMediaCodec_dequeueInputBuffer(codec_, kDequeueInputTimeoutUs);
        if (in_idx < 0) {
            ROBRT_LOGW("[mc_dec] dequeueInputBuffer failed: %zd", in_idx);
            DrainOutputs(render_time_ms, rtp_timestamp, kDrainOnInputBackpressureUs,
                         nullptr, video_frame_tracking_id);
            return;
        }

        size_t in_cap = 0;
        uint8_t* in_buf = AMediaCodec_getInputBuffer(codec_, static_cast<size_t>(in_idx), &in_cap);
        if (!in_buf || feed_sz > in_cap) {
            ROBRT_LOGW("[mc_dec] input buffer too small: need=%zu cap=%zu", feed_sz, in_cap);
            AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(in_idx), 0, 0, 0, 0);
            return;
        }
        memcpy(in_buf, feed_ptr, feed_sz);

        uint32_t flags = 0;
        if (is_keyframe) flags |= AMEDIACODEC_BUFFER_FLAG_KEY_FRAME;
        const int64_t pts_us = next_input_pts_us_++;

        media_status_t st = AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(in_idx),
                                                           0, feed_sz, pts_us, flags);
        if (st != AMEDIA_OK) {
            ROBRT_LOGW("[mc_dec] queueInputBuffer failed: %d", static_cast<int>(st));
            AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(in_idx), 0, 0, 0, 0);
            return;
        }

        int delivered0 = 0;
        DrainOutputs(render_time_ms, rtp_timestamp, 0, &delivered0, video_frame_tracking_id);
        if (delivered0 > 0) {
            DrainOutputs(render_time_ms, rtp_timestamp, 0, nullptr, video_frame_tracking_id);
        } else {
            DrainOutputs(render_time_ms, rtp_timestamp, kDrainAfterQueueShortWaitUs,
                         nullptr, video_frame_tracking_id);
        }
    }
};

AndroidMediaCodecVideoDecoder::AndroidMediaCodecVideoDecoder()
    : impl_(std::make_unique<AndroidMediaCodecVideoDecoder::Impl>()) {}

AndroidMediaCodecVideoDecoder::~AndroidMediaCodecVideoDecoder() {
    Release();
}

bool AndroidMediaCodecVideoDecoder::Configure(const webrtc::VideoDecoder::Settings& settings) {
    if (!impl_) return false;
    {
        std::lock_guard<std::mutex> lk(impl_->mu_);
        if (!impl_->running_) {
            impl_->running_ = true;
            impl_->thread_  = std::thread([this] { impl_->WorkerLoop(); });
        }
    }
    // std::function 要求可拷贝；packaged_task 仅可移动，故用 shared_ptr 包一层。
    auto pt = std::make_shared<std::packaged_task<bool()>>(
        [this, settings] { return impl_->ConfigureOnWorker(settings); });
    std::future<bool> fut = pt->get_future();
    {
        std::lock_guard<std::mutex> lk(impl_->mu_);
        impl_->tasks_.clear();
        impl_->tasks_.push_front([pt]() { (*pt)(); });
    }
    impl_->cv_.notify_one();
    return fut.get();
}

int32_t AndroidMediaCodecVideoDecoder::Decode(const webrtc::EncodedImage& input_image,
                                              bool /*missing_frames*/,
                                              int64_t render_time_ms) {
    if (!impl_) return WEBRTC_VIDEO_CODEC_ERROR;
    const size_t sz = input_image.size();
    if (sz == 0 || !input_image.data()) {
        static std::atomic<int> empty_warn{0};
        if (empty_warn.fetch_add(1) < 5) {
            ROBRT_LOGW("[mc_dec] Decode called empty: sz=%zu", sz);
        }
        return WEBRTC_VIDEO_CODEC_ERROR;
    }
    const uint32_t rtp_ts = input_image.RtpTimestamp();
    const bool key = (input_image.FrameType() == webrtc::VideoFrameType::kVideoFrameKey);
    const std::optional<uint16_t> tracking_id = input_image.VideoFrameTrackingId();

    std::vector<uint8_t> copy(sz);
    memcpy(copy.data(), input_image.data(), sz);

    {
        std::lock_guard<std::mutex> lk(impl_->mu_);
        if (!impl_->running_) return WEBRTC_VIDEO_CODEC_ERROR;
        impl_->tasks_.push_back(
            [this, copy = std::move(copy), render_time_ms, rtp_ts, key, tracking_id]() mutable {
                impl_->ProcessOneFrame(copy, render_time_ms, rtp_ts, key, tracking_id);
            });
    }
    impl_->cv_.notify_one();
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t AndroidMediaCodecVideoDecoder::RegisterDecodeCompleteCallback(
    webrtc::DecodedImageCallback* callback) {
    if (!impl_) return WEBRTC_VIDEO_CODEC_ERROR;
    std::lock_guard<std::mutex> lk(impl_->mu_);
    impl_->callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t AndroidMediaCodecVideoDecoder::Release() {
    if (!impl_) return WEBRTC_VIDEO_CODEC_OK;
    {
        std::lock_guard<std::mutex> lk(impl_->mu_);
        impl_->callback_ = nullptr;
    }
    auto pt = std::make_shared<std::packaged_task<void()>>([this] { impl_->DestroyCodec(); });
    std::future<void> fut = pt->get_future();
    {
        std::lock_guard<std::mutex> lk(impl_->mu_);
        if (impl_->running_) {
            impl_->tasks_.clear();
            impl_->tasks_.push_front([pt]() { (*pt)(); });
        }
    }
    impl_->cv_.notify_one();
    if (impl_->running_) fut.wait();
    impl_->StopWorker();
    return WEBRTC_VIDEO_CODEC_OK;
}

webrtc::VideoDecoder::DecoderInfo AndroidMediaCodecVideoDecoder::GetDecoderInfo() const {
    DecoderInfo info;
    info.implementation_name     = "mediacodec-h264";
    info.is_hardware_accelerated = true;
    return info;
}

}  // namespace robrt::client::impl

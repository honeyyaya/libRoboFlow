/**
 * @file  mediacodec_video_decoder.cpp
 * @brief Android AMediaCodec H.264 解码器实现（AHardwareBuffer 单路径，要求 API >= 26）。
 *
 * 关键设计：
 *   - 解码输出经由 AImageReader 直接产出 AHardwareBuffer，禁用 CPU NV12->I420 路径；
 *     API < 26 时 Configure 直接失败，由 factory 回退到内置软解。
 *   - input worker 与 output drain 线程解耦：worker 仅做 dequeue+queueInputBuffer；
 *     output drain 单独跑 dequeueOutputBuffer + AImageReader_acquireLatestImageAsync。
 *   - 元数据严格 PTS 关联（AllocateInputPtsUs 返回单调 us），避免 RTP ts 回绕导致的 stale。
 *   - 帧老化阈值：>33ms 直接丢弃（防止下游堵塞回灌）。
 *   - 输入背压：dequeueInputBuffer 容许 ~4ms 等待，Codec2 4 帧 in-flight 时不会被打断。
 *   - MediaFormat 设置：low-latency / output-delay=0 / 实时优先级 / Qualcomm Codec2 vendor key。
 */

#include "platform/android/mediacodec_video_decoder.h"
#include "platform/android/native_dec_frame_buffer.h"

#include "base/logging.h"
#include "base/timing_log.h"

#include <android/api-level.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "api/video/encoded_image.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_type.h"
#include "modules/video_coding/include/video_error_codes.h"

// NDK r26+ 的 NdkMediaCodec.h 不再定义 KEY_FRAME；与 Java MediaCodec.BUFFER_FLAG_KEY_FRAME 一致。
#ifndef AMEDIACODEC_BUFFER_FLAG_KEY_FRAME
#define AMEDIACODEC_BUFFER_FLAG_KEY_FRAME 1u
#endif

namespace rflow::rtc {

namespace {

// 与 Java MediaFormat.KEY_LOW_LATENCY 一致；部分设备在 API 30+ 上可降低解码器内部排队。
constexpr char kMediaFormatLowLatency[]    = "low-latency";
// API 31+：明确告知解码器无需重排序，输出可立即就绪。
constexpr char kMediaFormatOutputDelay[]   = "output-delay";
// KEY_OPERATING_RATE / KEY_PRIORITY：实时优先级 + 不限速运行。
constexpr char kMediaFormatOperatingRate[] = "operating-rate";
constexpr char kMediaFormatPriority[]      = "priority";
// Qualcomm Codec2 vendor 扩展：低延迟、按解码顺序输出、关 VPP 后处理。
// 不识别这些 key 的设备会静默忽略，安全。
constexpr char kVendorQtiLowLatencyEnable[]    = "vendor.qti-ext-dec-low-latency.enable";
constexpr char kVendorQtiPictureOrderEnable[]  = "vendor.qti-ext-dec-picture-order.enable";
constexpr char kVendorQtiVppEnable[]           = "vendor.qti-ext-vpp.enable";
constexpr int  kCodecLowLatencyMinApi          = 30;

// dequeueInputBuffer 短阻塞：0us 会让任何瞬时 input ring full 直接丢帧（参考链断），
// 4ms 在 4 帧 in-flight 的常态下仍然足够吸收，且不影响整体延迟。
constexpr int64_t kDequeueInputTimeoutUs       = 4000;
constexpr int64_t kBackpressureLogIntervalUs   = 500000;

// 输出帧老化阈值：超过 ~2 vsync 直接丢，让链路紧跟最新输入，避免堆积反馈。
constexpr int64_t kOutputFrameAgeDropThresholdUs = 33 * 1000;

// AImageReader 池规模：12 与 demo 经验值对齐。槽位被 AndroidNativeDecFrameBuffer 持有，
// 直到业务侧 release_video_frame；过小会出现 "Unable to acquire a lockedBuffer" 噪声。
constexpr int32_t kImageReaderMaxImages          = 12;
// pending output metadata 上限：>= kImageReaderMaxImages 即可，太大会跟踪历史背压尾巴。
constexpr size_t  kMaxPendingOutputMetadata      = 16;

int64_t McMonotonicUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// 以 system_clock 抓当前墙钟时间，写入 "YYYY-MM-DD HH:MM:SS.mmm" 串。
// 仅在打 [Timing/Decode] 等采样日志时调用，热路径上不会触发。
void FormatWallClockNow(char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    const auto now_sys = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now_sys);
    const auto ms_part = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now_sys.time_since_epoch()) % 1000;
    std::tm tm_local{};
#if defined(_WIN32)
    localtime_s(&tm_local, &t);
#else
    localtime_r(&t, &tm_local);
#endif
    std::snprintf(out, out_sz, "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
                  tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday,
                  tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec,
                  static_cast<long long>(ms_part.count()));
}

int GetDeviceApiLevel() {
    const int api_level = android_get_device_api_level();
    return api_level > 0 ? api_level : __ANDROID_API__;
}

bool ShouldRequestLowLatencyCodec() {
    return GetDeviceApiLevel() >= kCodecLowLatencyMinApi;
}

// WebRTC H264 接收路径多为 Annex B（00 00 01 / 00 00 00 01）；Codec2 通常需要 Annex B。
// 误转 AVCC（4 字节长度前缀）会让部分机型吃满 input 但永远不出 output（fps=0）。
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
                (data[j + 2] == 1 ||
                 (j + 4 <= size && data[j + 2] == 0 && data[j + 3] == 1))) {
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

}  // namespace

struct AndroidMediaCodecVideoDecoder::Impl {
    struct OutputFrameMetadata {
        int64_t                 render_time_ms        = 0;
        uint32_t                rtp_timestamp         = 0;
        int64_t                 decode_wall_t0_us     = 0;
        std::optional<uint16_t> video_frame_tracking_id;
    };

    struct PendingOutputFrameMetadata {
        int64_t            pts_us = 0;
        OutputFrameMetadata metadata;
    };

    std::mutex                        mu_;
    std::condition_variable           cv_;
    std::deque<std::function<void()>> tasks_;
    bool                              running_ = false;
    std::thread                       thread_;
    std::atomic<bool>                 output_running_{false};
    std::thread                       output_thread_;

    AMediaCodec*   codec_         = nullptr;
    AImageReader*  image_reader_  = nullptr;
    ANativeWindow* output_window_ = nullptr;

    webrtc::DecodedImageCallback* callback_ = nullptr;

    int out_width_  = 0;
    int out_height_ = 0;

    std::vector<uint8_t>                avcc_scratch_;
    std::deque<PendingOutputFrameMetadata> pending_output_metadata_;
    std::atomic<int32_t>                pending_image_notifications_{0};

    int64_t  next_input_pts_us_         = 0;
    size_t   pending_decode_tasks_      = 0;
    uint32_t dequeue_input_fail_burst_  = 0;
    uint64_t dequeue_input_fail_total_  = 0;
    int64_t  last_backpressure_log_us_  = 0;

    // ---------------------------------------------------------------------
    // Worker (input) thread
    // ---------------------------------------------------------------------
    void WorkerLoop() {
        std::unique_lock<std::mutex> lk(mu_);
        while (running_) {
            cv_.wait(lk, [this] { return !tasks_.empty() || !running_; });
            if (!running_) break;
            while (!tasks_.empty()) {
                std::function<void()> job = std::move(tasks_.front());
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
            tasks_.clear();
            pending_decode_tasks_ = 0;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    // ---------------------------------------------------------------------
    // Dedicated output drain thread (decoupled from input worker)
    // ---------------------------------------------------------------------
    void OutputDrainLoop() {
        RFLOW_LOGI("[mc_dec] output drain thread started");
        while (output_running_.load(std::memory_order_acquire)) {
            if (!codec_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            DrainOutputsBounded(1000, 1, nullptr);
#if __ANDROID_API__ >= 26
            if (pending_image_notifications_.load(std::memory_order_relaxed) > 0) {
                DrainReadyImages(nullptr);
            }
#endif
        }
        RFLOW_LOGI("[mc_dec] output drain thread stopped");
    }

    void StartOutputDrainThread() {
        bool expected = false;
        if (!output_running_.compare_exchange_strong(expected, true,
                                                      std::memory_order_acq_rel)) {
            return;
        }
        output_thread_ = std::thread([this] { OutputDrainLoop(); });
    }

    void StopOutputDrainThread() {
        if (!output_running_.exchange(false, std::memory_order_acq_rel)) return;
        if (output_thread_.joinable() &&
            output_thread_.get_id() != std::this_thread::get_id()) {
            output_thread_.join();
        }
    }

    // ---------------------------------------------------------------------
    // Input backpressure logging
    // ---------------------------------------------------------------------
    void ResetInputBackpressureBurst() {
        std::lock_guard<std::mutex> lk(mu_);
        dequeue_input_fail_burst_ = 0;
    }

    void MaybeLogInputBackpressure(ssize_t in_idx) {
        const int64_t now_us = McMonotonicUs();
        size_t   pending_decode_tasks  = 0;
        size_t   pending_output_meta   = 0;
        uint32_t burst                 = 0;
        uint64_t total                 = 0;
        bool     should_log            = false;
        {
            std::lock_guard<std::mutex> lk(mu_);
            ++dequeue_input_fail_burst_;
            ++dequeue_input_fail_total_;
            burst                = dequeue_input_fail_burst_;
            total                = dequeue_input_fail_total_;
            pending_decode_tasks = pending_decode_tasks_;
            pending_output_meta  = pending_output_metadata_.size();
            should_log =
                burst <= 3 || (now_us - last_backpressure_log_us_) >= kBackpressureLogIntervalUs;
            if (should_log) last_backpressure_log_us_ = now_us;
        }
        if (!should_log) return;

        RFLOW_LOGW(
            "[mc_dec] backpressure dequeueInputBuffer=%zd burst=%u total=%llu "
            "pending_tasks=%zu pending_meta=%zu pending_images=%d out=%dx%d "
            "(frame DROPPED -> reference chain may break)",
            in_idx, burst, static_cast<unsigned long long>(total), pending_decode_tasks,
            pending_output_meta,
            pending_image_notifications_.load(std::memory_order_relaxed),
            out_width_, out_height_);
    }

    // ---------------------------------------------------------------------
    // Codec / ImageReader lifecycle
    // ---------------------------------------------------------------------
    void DestroyCodec() {
        StopOutputDrainThread();
        if (codec_) {
            AMediaCodec_stop(codec_);
            AMediaCodec_delete(codec_);
            codec_ = nullptr;
        }
        if (image_reader_) {
#if __ANDROID_API__ >= 26
            DetachImageReaderListener();
#endif
            AImageReader_delete(image_reader_);
            image_reader_  = nullptr;
            output_window_ = nullptr;
        } else {
#if __ANDROID_API__ >= 26
            ResetImageReaderState();
#endif
        }
        out_width_ = out_height_ = 0;
    }

    void UpdateOutputFormat(AMediaFormat* fmt) {
        if (!fmt) return;
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH,  &out_width_);
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &out_height_);
    }

    void RefreshOutputFormat() {
        if (!codec_) return;
        AMediaFormat* fmt = AMediaCodec_getOutputFormat(codec_);
        if (fmt) {
            UpdateOutputFormat(fmt);
            AMediaFormat_delete(fmt);
        }
    }

    void ClearOutputMetadata() {
        std::lock_guard<std::mutex> lk(mu_);
        pending_output_metadata_.clear();
    }

    int64_t AllocateInputPtsUs() {
        const int64_t now_us = McMonotonicUs();
        if (now_us <= next_input_pts_us_) {
            ++next_input_pts_us_;
        } else {
            next_input_pts_us_ = now_us;
        }
        return next_input_pts_us_;
    }

    void RecordOutputMetadata(int64_t pts_us,
                              int64_t render_time_ms,
                              uint32_t rtp_timestamp,
                              int64_t decode_wall_t0_us,
                              const std::optional<uint16_t>& tracking_id) {
        std::lock_guard<std::mutex> lk(mu_);
        OutputFrameMetadata meta;
        meta.render_time_ms          = render_time_ms;
        meta.rtp_timestamp           = rtp_timestamp;
        meta.decode_wall_t0_us       = decode_wall_t0_us;
        meta.video_frame_tracking_id = tracking_id;
        pending_output_metadata_.push_back({pts_us, meta});
        while (pending_output_metadata_.size() > kMaxPendingOutputMetadata) {
            pending_output_metadata_.pop_front();
        }
    }

    void RemoveOutputMetadata(int64_t pts_us) {
        std::lock_guard<std::mutex> lk(mu_);
        if (!pending_output_metadata_.empty() &&
            pending_output_metadata_.back().pts_us == pts_us) {
            pending_output_metadata_.pop_back();
            return;
        }
        for (auto it = pending_output_metadata_.begin();
             it != pending_output_metadata_.end(); ++it) {
            if (it->pts_us == pts_us) {
                pending_output_metadata_.erase(it);
                return;
            }
        }
    }

    void DiscardOutputMetadataUpToPtsUsLocked(int64_t pts_us) {
        while (!pending_output_metadata_.empty() &&
               pending_output_metadata_.front().pts_us <= pts_us) {
            pending_output_metadata_.pop_front();
        }
    }

    void DiscardOutputMetadataUpToPtsUs(int64_t pts_us) {
        std::lock_guard<std::mutex> lk(mu_);
        DiscardOutputMetadataUpToPtsUsLocked(pts_us);
    }

    std::optional<OutputFrameMetadata> TakeOutputMetadataForPtsUs(int64_t pts_us) {
        std::lock_guard<std::mutex> lk(mu_);
        static std::atomic<int> stale_warn{0};
        while (!pending_output_metadata_.empty() &&
               pending_output_metadata_.front().pts_us < pts_us) {
            if (stale_warn.fetch_add(1, std::memory_order_relaxed) < 5) {
                RFLOW_LOGW("[mc_dec] drop stale meta: expected pts=%lld got newer pts=%lld",
                           static_cast<long long>(pending_output_metadata_.front().pts_us),
                           static_cast<long long>(pts_us));
            }
            pending_output_metadata_.pop_front();
        }
        if (!pending_output_metadata_.empty() &&
            pending_output_metadata_.front().pts_us == pts_us) {
            OutputFrameMetadata meta = pending_output_metadata_.front().metadata;
            pending_output_metadata_.pop_front();
            return meta;
        }
        static std::atomic<int> miss_warn{0};
        if (miss_warn.fetch_add(1, std::memory_order_relaxed) < 5) {
            RFLOW_LOGW("[mc_dec] output pts metadata missing: pts=%lld",
                       static_cast<long long>(pts_us));
        }
        return std::nullopt;
    }

#if __ANDROID_API__ >= 26
    static void OnImageAvailable(void* context, AImageReader* /*reader*/) {
        auto* self = static_cast<Impl*>(context);
        if (!self) return;
        self->pending_image_notifications_.fetch_add(1, std::memory_order_relaxed);
    }

    void ResetImageReaderState() {
        pending_image_notifications_.store(0, std::memory_order_relaxed);
    }

    void DetachImageReaderListener() {
        if (!image_reader_) {
            ResetImageReaderState();
            return;
        }
        AImageReader_ImageListener listener{};
        listener.context         = nullptr;
        listener.onImageAvailable = nullptr;
        AImageReader_setImageListener(image_reader_, &listener);
        ResetImageReaderState();
    }

    bool InstallImageReaderListener() {
        if (!image_reader_) return false;
        AImageReader_ImageListener listener{};
        listener.context         = this;
        listener.onImageAvailable = &Impl::OnImageAvailable;
        const media_status_t st = AImageReader_setImageListener(image_reader_, &listener);
        if (st != AMEDIA_OK) {
            RFLOW_LOGW("[mc_dec] AImageReader_setImageListener failed: %d",
                       static_cast<int>(st));
            ResetImageReaderState();
            return false;
        }
        ResetImageReaderState();
        return true;
    }

    bool AcquireLatestOutputImage(AImage** out_image, int* out_sync_fence_fd) {
        if (!image_reader_ || !out_image || !out_sync_fence_fd) return false;
        *out_image          = nullptr;
        *out_sync_fence_fd  = -1;

        const media_status_t st = AImageReader_acquireLatestImageAsync(
            image_reader_, out_image, out_sync_fence_fd);
        if (st == AMEDIA_OK && *out_image) {
            ResetImageReaderState();
            return true;
        }
        if (*out_sync_fence_fd >= 0) {
            close(*out_sync_fence_fd);
            *out_sync_fence_fd = -1;
        }
        if (st == AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE ||
            st == AMEDIA_IMGREADER_MAX_IMAGES_ACQUIRED) {
            ResetImageReaderState();
            return false;
        }
        static std::atomic<int> warn{0};
        if (warn.fetch_add(1, std::memory_order_relaxed) < 5) {
            RFLOW_LOGW("[mc_dec] AImageReader_acquireLatestImageAsync failed: %d",
                       static_cast<int>(st));
        }
        return false;
    }

    void DrainReadyImages(int* delivered_frames) {
        if (!image_reader_) return;

        for (;;) {
            webrtc::DecodedImageCallback* cb = nullptr;
            {
                std::lock_guard<std::mutex> lk(mu_);
                cb = callback_;
            }
            if (!cb) return;

            AImage* image       = nullptr;
            int     fence_fd    = -1;
            if (!AcquireLatestOutputImage(&image, &fence_fd) || !image) return;

            AHardwareBuffer* hw = nullptr;
            const media_status_t hw_st = AImage_getHardwareBuffer(image, &hw);
            if (hw_st != AMEDIA_OK || !hw) {
                if (fence_fd >= 0) close(fence_fd);
                AImage_delete(image);
                RFLOW_LOGW("[mc_dec] AImage_getHardwareBuffer failed: %d",
                           static_cast<int>(hw_st));
                continue;
            }

            int32_t image_w = out_width_;
            int32_t image_h = out_height_;
            AImage_getWidth (image, &image_w);
            AImage_getHeight(image, &image_h);
            if (image_w <= 0 || image_h <= 0) {
                if (fence_fd >= 0) close(fence_fd);
                AImage_delete(image);
                RFLOW_LOGW("[mc_dec] AImage dimensions invalid: %d x %d", image_w, image_h);
                continue;
            }

            int64_t ts_ns = 0;
            const media_status_t ts_st = AImage_getTimestamp(image, &ts_ns);
            if (ts_st != AMEDIA_OK) {
                if (fence_fd >= 0) close(fence_fd);
                AImage_delete(image);
                static std::atomic<int> warn{0};
                if (warn.fetch_add(1, std::memory_order_relaxed) < 5) {
                    RFLOW_LOGW("[mc_dec] AImage_getTimestamp failed: %d",
                               static_cast<int>(ts_st));
                }
                continue;
            }
            const int64_t pts_us = ts_ns / 1000;

            const std::optional<OutputFrameMetadata> meta = TakeOutputMetadataForPtsUs(pts_us);
            if (!meta.has_value()) {
                if (fence_fd >= 0) close(fence_fd);
                AImage_delete(image);
                continue;
            }

            // 老化丢帧：超过 ~2 vsync 还没出口的帧直接丢，避免下游堆积形成持续延迟。
            const int64_t now_us = McMonotonicUs();
            if (meta->decode_wall_t0_us > 0 &&
                now_us - meta->decode_wall_t0_us > kOutputFrameAgeDropThresholdUs) {
                static std::atomic<int> drop_warn{0};
                if (drop_warn.fetch_add(1, std::memory_order_relaxed) < 5) {
                    RFLOW_LOGW("[mc_dec] frame too late, drop. age=%lldms rtp_ts=%u",
                               static_cast<long long>((now_us - meta->decode_wall_t0_us) / 1000),
                               meta->rtp_timestamp);
                }
                if (fence_fd >= 0) close(fence_fd);
                AImage_delete(image);
                continue;
            }

            // AndroidNativeDecFrameBuffer 接管 image / hw / fence_fd 的生命周期。
            auto native = AndroidNativeDecFrameBuffer::Create(image, hw, fence_fd,
                                                               image_w, image_h);
            if (!native) {
                if (fence_fd >= 0) close(fence_fd);
                AImage_delete(image);
                continue;
            }

            webrtc::VideoFrame::Builder fb;
            fb.set_video_frame_buffer(native)
              .set_rtp_timestamp(meta->rtp_timestamp)
              .set_timestamp_us(meta->render_time_ms * 1000);
            if (meta->video_frame_tracking_id.has_value()) {
                fb.set_id(*meta->video_frame_tracking_id);
            }
            webrtc::VideoFrame frame = fb.build();
            cb->Decoded(frame);
            if (delivered_frames) ++(*delivered_frames);
        }
    }
#else
    void ResetImageReaderState() {}
    void DetachImageReaderListener() {}
    bool InstallImageReaderListener() { return false; }
    void DrainReadyImages(int*) {}
#endif

    // ---------------------------------------------------------------------
    // Configure
    // ---------------------------------------------------------------------
    bool ConfigureOnWorker(const webrtc::VideoDecoder::Settings& settings) {
        DestroyCodec();
        ClearOutputMetadata();

        int w = 1920;
        int h = 1080;
        if (settings.max_render_resolution().Valid()) {
            w = settings.max_render_resolution().Width();
            h = settings.max_render_resolution().Height();
        }

#if __ANDROID_API__ < 26
        RFLOW_LOGW("[mc_dec] AHardwareBuffer decoder requires Android API 26+");
        return false;
#else
        codec_ = AMediaCodec_createDecoderByType("video/avc");
        if (!codec_) {
            RFLOW_LOGW("[mc_dec] AMediaCodec_createDecoderByType failed");
            return false;
        }

        AMediaFormat* format = AMediaFormat_new();
        AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME,   "video/avc");
        AMediaFormat_setInt32 (format, AMEDIAFORMAT_KEY_WIDTH,  w);
        AMediaFormat_setInt32 (format, AMEDIAFORMAT_KEY_HEIGHT, h);

        const bool req_low_latency = ShouldRequestLowLatencyCodec();
        if (req_low_latency) {
            AMediaFormat_setInt32(format, kMediaFormatLowLatency, 1);
            AMediaFormat_setInt32(format, kMediaFormatOutputDelay, 0);
        }
        // 实时优先级 + 不限速运行（KEY_PRIORITY=0；KEY_OPERATING_RATE=SHRT_MAX 等价 0x7FFF）。
        AMediaFormat_setInt32(format, kMediaFormatPriority, 0);
        AMediaFormat_setInt32(format, kMediaFormatOperatingRate, 0x7FFF);
        // Qualcomm Codec2：低延迟、按解码顺序、关 VPP 后处理。其它机型静默忽略。
        AMediaFormat_setInt32(format, kVendorQtiLowLatencyEnable,   1);
        AMediaFormat_setInt32(format, kVendorQtiPictureOrderEnable, 1);
        AMediaFormat_setInt32(format, kVendorQtiVppEnable,          0);

        media_status_t st = AImageReader_newWithUsage(
            w, h, AIMAGE_FORMAT_PRIVATE,
            AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT,
            kImageReaderMaxImages, &image_reader_);
        if (st != AMEDIA_OK || !image_reader_) {
            AMediaFormat_delete(format);
            RFLOW_LOGW("[mc_dec] AImageReader_newWithUsage failed: %d", static_cast<int>(st));
            AMediaCodec_delete(codec_);
            codec_ = nullptr;
            return false;
        }
        InstallImageReaderListener();
        st = AImageReader_getWindow(image_reader_, &output_window_);
        if (st != AMEDIA_OK || !output_window_) {
            AMediaFormat_delete(format);
            RFLOW_LOGW("[mc_dec] AImageReader_getWindow failed: %d", static_cast<int>(st));
            DestroyCodec();
            return false;
        }

        st = AMediaCodec_configure(codec_, format, output_window_, nullptr, 0);
        AMediaFormat_delete(format);
        if (st != AMEDIA_OK) {
            RFLOW_LOGW("[mc_dec] AMediaCodec_configure failed: %d", static_cast<int>(st));
            DestroyCodec();
            return false;
        }
        st = AMediaCodec_start(codec_);
        if (st != AMEDIA_OK) {
            RFLOW_LOGW("[mc_dec] AMediaCodec_start failed: %d", static_cast<int>(st));
            DestroyCodec();
            return false;
        }

        StartOutputDrainThread();
        next_input_pts_us_ = 0;
        {
            std::lock_guard<std::mutex> lk(mu_);
            dequeue_input_fail_burst_  = 0;
            dequeue_input_fail_total_  = 0;
            last_backpressure_log_us_  = 0;
        }
        RFLOW_LOGI(
            "[mc_dec] configured codec=video/avc size=%dx%d max_images=%d max_meta=%zu "
            "drop_age_ms=%lld deq_in_us=%lld low_latency=%d output_delay=%d "
            "operating_rate=0x7FFF priority=0 qti_low_latency=1 qti_picture_order=1 "
            "qti_vpp=0 api=%d",
            w, h, static_cast<int>(kImageReaderMaxImages),
            static_cast<size_t>(kMaxPendingOutputMetadata),
            static_cast<long long>(kOutputFrameAgeDropThresholdUs / 1000),
            static_cast<long long>(kDequeueInputTimeoutUs),
            req_low_latency ? 1 : 0,
            req_low_latency ? 0 : -1,
            GetDeviceApiLevel());
        // 尽早探到输出格式，部分 Codec2 在首帧前不会单独触发 INFO，导致 out_width_ 长期为 0。
        RefreshOutputFormat();
        return true;
#endif
    }

    // ---------------------------------------------------------------------
    // Output drain (codec output -> ImageReader buffer queue)
    // ---------------------------------------------------------------------
    void DrainOutputs(int64_t first_dequeue_timeout_us, int* delivered_frames) {
        if (!codec_) return;

        bool used_timeout            = false;
        bool released_to_native_surf = false;

        for (;;) {
            AMediaCodecBufferInfo info;
            const int64_t t_us = used_timeout ? 0 : first_dequeue_timeout_us;
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

            webrtc::DecodedImageCallback* cb = nullptr;
            {
                std::lock_guard<std::mutex> lk(mu_);
                cb = callback_;
            }
            const bool use_native_surface = cb && image_reader_;
            released_to_native_surf = released_to_native_surf || use_native_surface;

            // 单路径：始终走 native surface（image_reader_）；release(false) 仅在没有 callback 时。
            AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(out_idx),
                                            use_native_surface);
#if __ANDROID_API__ >= 26
            if (!use_native_surface) {
                DiscardOutputMetadataUpToPtsUs(info.presentationTimeUs);
            }
#endif
        }
#if __ANDROID_API__ >= 26
        if (released_to_native_surf) {
            DrainReadyImages(delivered_frames);
        }
#endif
    }

    int DrainOutputsBounded(int64_t first_dequeue_timeout_us, int max_passes,
                            int* delivered_frames_total) {
        int total = 0;
        bool first = true;
        for (int i = 0; i < max_passes; ++i) {
            int delivered_this_pass = 0;
            DrainOutputs(first ? first_dequeue_timeout_us : 0, &delivered_this_pass);
            total += delivered_this_pass;
            if (delivered_this_pass == 0) break;
            first = false;
        }
        if (delivered_frames_total) *delivered_frames_total += total;
        return total;
    }

    // ---------------------------------------------------------------------
    // Per-frame input processing (worker thread)
    // ---------------------------------------------------------------------
    void ProcessOneFrame(
        const webrtc::scoped_refptr<webrtc::EncodedImageBufferInterface>& data,
        size_t   data_size,
        int64_t  render_time_ms,
        uint32_t rtp_timestamp,
        bool     is_keyframe,
        int64_t  decode_wall_t0_us,
        const std::optional<uint16_t>& tracking_id) {
        if (!codec_ || !data || data_size == 0 || !data->data()) return;

        // 采样判定：仅当 tracking_id 存在且命中采样周期，并且 RFLOW_LOG_TIMING 开启
        // 时才进行细粒度计时。未命中时所有 timer 取值都不计算，热路径零开销。
        const bool log_timing =
            tracking_id.has_value() &&
            rflow::timing_log::ShouldSampleByTrackingId(
                static_cast<uint32_t>(*tracking_id));
        const uint32_t tid_for_log =
            tracking_id.has_value() ? static_cast<uint32_t>(*tracking_id) : 0u;

        // worker_queue：从 Decode() 入口（decode_wall_t0_us）到 worker 真正 picked 的延迟。
        const int64_t t_worker_in_us = log_timing ? McMonotonicUs() : int64_t{0};
        const double  worker_queue_ms =
            log_timing ? (t_worker_in_us - decode_wall_t0_us) / 1000.0 : 0.0;

        const bool input_is_annexb = LooksLikeAnnexB(data->data(), data_size);
        const int64_t t_prep0 = log_timing ? McMonotonicUs() : int64_t{0};
        const uint8_t* feed_ptr = data->data();
        size_t         feed_sz  = data_size;
        if (!input_is_annexb) {
            AnnexBToAvcc(data->data(), data_size, &avcc_scratch_);
            if (avcc_scratch_.empty()) return;
            feed_ptr = avcc_scratch_.data();
            feed_sz  = avcc_scratch_.size();
        }
        const double prepare_ms =
            log_timing ? (McMonotonicUs() - t_prep0) / 1000.0 : 0.0;

        const int64_t t_deqin0 = log_timing ? McMonotonicUs() : int64_t{0};
        ssize_t in_idx = AMediaCodec_dequeueInputBuffer(codec_, kDequeueInputTimeoutUs);
        const double deq_in_ms =
            log_timing ? (McMonotonicUs() - t_deqin0) / 1000.0 : 0.0;
        if (in_idx < 0) {
            MaybeLogInputBackpressure(in_idx);
            if (log_timing) {
                char wall[40] = {0};
                FormatWallClockNow(wall, sizeof(wall));
                const double total_ms =
                    (McMonotonicUs() - decode_wall_t0_us) / 1000.0;
                RFLOW_LOGI(
                    "[Timing/DecodeDrop] tracking_id=%u rtp_ts=%u key=%d bytes=%zu "
                    "annexb=%d wall=%s | worker_queue=%.3f ms | prepare=%.3f ms | "
                    "deq_in=%.3f ms | total=%.3f ms",
                    tid_for_log, rtp_timestamp, is_keyframe ? 1 : 0, feed_sz,
                    input_is_annexb ? 1 : 0, wall,
                    worker_queue_ms, prepare_ms, deq_in_ms, total_ms);
            }
            return;
        }
        ResetInputBackpressureBurst();

        size_t   in_cap = 0;
        uint8_t* in_buf =
            AMediaCodec_getInputBuffer(codec_, static_cast<size_t>(in_idx), &in_cap);
        if (!in_buf || feed_sz > in_cap) {
            RFLOW_LOGW("[mc_dec] input buffer too small: need=%zu cap=%zu", feed_sz, in_cap);
            AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(in_idx), 0, 0, 0, 0);
            return;
        }
        const int64_t t_copy0 = log_timing ? McMonotonicUs() : int64_t{0};
        memcpy(in_buf, feed_ptr, feed_sz);
        const double copy_ms =
            log_timing ? (McMonotonicUs() - t_copy0) / 1000.0 : 0.0;

        uint32_t flags = 0;
        if (is_keyframe) flags |= AMEDIACODEC_BUFFER_FLAG_KEY_FRAME;
        const int64_t pts_us = AllocateInputPtsUs();

        RecordOutputMetadata(pts_us, render_time_ms, rtp_timestamp, decode_wall_t0_us,
                             tracking_id);

        const int64_t t_qin0 = log_timing ? McMonotonicUs() : int64_t{0};
        media_status_t st = AMediaCodec_queueInputBuffer(codec_,
                                                         static_cast<size_t>(in_idx),
                                                         0, feed_sz, pts_us, flags);
        const double q_in_ms =
            log_timing ? (McMonotonicUs() - t_qin0) / 1000.0 : 0.0;
        if (st != AMEDIA_OK) {
            RFLOW_LOGW("[mc_dec] queueInputBuffer failed: %d", static_cast<int>(st));
            RemoveOutputMetadata(pts_us);
            // 已 dequeue 的 input 必须归还，否则 codec 内部状态错乱并放大系统层 PipelineWatcher 告警。
            AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(in_idx), 0, 0, 0, 0);
            return;
        }
        // 输出由独立 drain 线程拉取，这里不直接 drain，避免 worker 串行阻塞。

        if (log_timing) {
            char wall[40] = {0};
            FormatWallClockNow(wall, sizeof(wall));
            const double total_ms =
                (McMonotonicUs() - decode_wall_t0_us) / 1000.0;
            // 单行采样耗时分析；合并了原参考实现中的 [耗时分析]EncodedFrame 与
            // [Timing/Decode] 两条日志（McE2E native 见 1Hz [Pipeline/Latency]
            // 中的 decode_avg / processing_avg；端到端总耗时由渲染层负责打印）。
            RFLOW_LOGI(
                "[Timing/Decode] tracking_id=%u rtp_ts=%u key=%d bytes=%zu annexb=%d "
                "wall=%s | worker_queue=%.3f ms | prepare=%.3f ms | deq_in=%.3f ms | "
                "copy=%.3f ms | q_in=%.3f ms | total=%.3f ms",
                tid_for_log, rtp_timestamp, is_keyframe ? 1 : 0, feed_sz,
                input_is_annexb ? 1 : 0, wall,
                worker_queue_ms, prepare_ms, deq_in_ms, copy_ms, q_in_ms, total_ms);
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
        impl_->pending_decode_tasks_ = 0;
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
            RFLOW_LOGW("[mc_dec] Decode called empty: sz=%zu", sz);
        }
        return WEBRTC_VIDEO_CODEC_ERROR;
    }
    const uint32_t rtp_ts = input_image.RtpTimestamp();
    const bool     key    = (input_image.FrameType() == webrtc::VideoFrameType::kVideoFrameKey);
    const std::optional<uint16_t> tracking_id = input_image.VideoFrameTrackingId();

    // 端到端起点：与本帧 EncodedImage 对应，包含后续排队 / worker / Decoded 回调的全部时间。
    const int64_t decode_wall_t0_us = McMonotonicUs();

    webrtc::scoped_refptr<webrtc::EncodedImageBufferInterface> encoded_buffer =
        input_image.GetEncodedData();
    if (!encoded_buffer) {
        encoded_buffer = webrtc::EncodedImageBuffer::Create(input_image.data(), sz);
    }
    if (!encoded_buffer || encoded_buffer->size() < sz || !encoded_buffer->data()) {
        RFLOW_LOGW("[mc_dec] Decode buffer unavailable: sz=%zu", sz);
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    {
        std::lock_guard<std::mutex> lk(impl_->mu_);
        if (!impl_->running_) return WEBRTC_VIDEO_CODEC_ERROR;
        ++impl_->pending_decode_tasks_;
        impl_->tasks_.push_back([this, encoded_buffer = std::move(encoded_buffer),
                                 render_time_ms, encoded_size = sz, rtp_ts, key,
                                 decode_wall_t0_us, tracking_id]() mutable {
            impl_->ProcessOneFrame(encoded_buffer, encoded_size, render_time_ms, rtp_ts, key,
                                   decode_wall_t0_us, tracking_id);
            std::lock_guard<std::mutex> lk(impl_->mu_);
            if (impl_->pending_decode_tasks_ > 0) {
                --impl_->pending_decode_tasks_;
            }
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

    auto pt = std::make_shared<std::packaged_task<void()>>([this] {
        impl_->DestroyCodec();
        impl_->ClearOutputMetadata();
    });
    std::future<void> fut = pt->get_future();
    bool wait_for_destroy = false;
    {
        std::lock_guard<std::mutex> lk(impl_->mu_);
        if (impl_->running_) {
            impl_->tasks_.clear();
            impl_->pending_decode_tasks_ = 0;
            impl_->tasks_.push_front([pt]() { (*pt)(); });
            wait_for_destroy = true;
        }
    }
    impl_->cv_.notify_one();
    if (wait_for_destroy) {
        fut.wait();
    } else {
        impl_->DestroyCodec();
        impl_->ClearOutputMetadata();
    }
    impl_->StopOutputDrainThread();
    impl_->StopWorker();
    return WEBRTC_VIDEO_CODEC_OK;
}

webrtc::VideoDecoder::DecoderInfo AndroidMediaCodecVideoDecoder::GetDecoderInfo() const {
    DecoderInfo info;
    info.implementation_name     = "mediacodec-h264";
    info.is_hardware_accelerated = true;
    return info;
}

}  // namespace rflow::rtc

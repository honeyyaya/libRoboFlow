#include "media/camera_video_track_source.h"

#include "media/capture_fps_pipeline_policy.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_frame.h"
#include "base/env_reader.h"
#include "base/trace_switches.h"
#include "public/log_tagged.h"
#include "rtc/push_pipeline_drop_stats.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "media/zero_copy_pipeline_policy.h"
#include "modules/video_capture/video_capture_factory.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"

#include "libyuv/convert.h"

#if defined(WEBRTC_LINUX) && defined(__linux__)
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
#include "platform/rockchip/native_dec_frame_buffer.h"
#include "platform/rockchip/mjpeg_decoder.h"
#include <linux/dma-buf.h>
#endif
#include <cerrno>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#include <pthread.h>

#include <chrono>
#include <deque>
#include <functional>
#include <sstream>
#include <cstdlib>
#include <atomic>
#include <algorithm>
#endif

namespace rflow::service::impl {

namespace capture_policy = rflow::service::impl::policy;

#if defined(WEBRTC_LINUX) && defined(__linux__)
namespace {
bool LatencyTraceEnabled() {
    static const bool enabled = rflow::common::util::TraceFlagEnabled("RFLOW_LATENCY_TRACE");
    return enabled;
}

void ApplyThreadTuneIfRequested(const char* role, const char* cpu_env_name, int requested_fps) {
    const char* mode = std::getenv("RFLOW_MEDIA_THREAD_SCHED");
    const bool mode_off = mode && (mode[0] == '0' || mode[0] == 'n' || mode[0] == 'N' || mode[0] == 'f' || mode[0] == 'F');
    const bool mode_set = mode && mode[0];

    if (cpu_env_name) {
        const int cpu = rflow::common::util::ReadEnvIntInRange(cpu_env_name, -1, -1, 4096);
        if (cpu >= 0) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(cpu, &cpuset);
            const int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
            if (rc != 0) {
                RFLOW_LOG_TAG_E("ThreadTune", "%s setaffinity cpu=%d failed rc=%d", role, cpu, rc);
            } else {
                RFLOW_LOG_TAG_I("ThreadTune", "%s affinity cpu=%d", role, cpu);
            }
        }
    }
    if (mode_off) {
        return;
    }
    if (!mode_set) {
        if (capture_policy::ShouldAutoTuneMediaThreadsForFps(requested_fps)) {
            const int nice_val =
                rflow::common::util::ReadEnvIntInRange("RFLOW_MEDIA_THREAD_NICE", -10, -20, 19);
            if (setpriority(PRIO_PROCESS, 0, nice_val) == 0) {
                RFLOW_LOG_TAG_I("ThreadTune",
                                "%s auto high-fps nice=%d (request=%dfps; override RFLOW_MEDIA_THREAD_SCHED "
                                "or RFLOW_MEDIA_THREAD_AUTO=0 to disable)",
                                role, nice_val, requested_fps);
            } else {
                RFLOW_LOG_TAG_E("ThreadTune", "%s auto high-fps setpriority nice=%d failed errno=%d", role,
                                nice_val, errno);
            }
        }
        return;
    }

    const bool use_rr = mode && (mode[0] == 'r' || mode[0] == 'R');
    if (use_rr) {
        const int rr_prio = rflow::common::util::ReadEnvIntInRange("RFLOW_MEDIA_THREAD_RR_PRIO", 20, 1, 90);
        sched_param sp{};
        sp.sched_priority = rr_prio;
        const int rc = pthread_setschedparam(pthread_self(), SCHED_RR, &sp);
        if (rc != 0) {
            RFLOW_LOG_TAG_E("ThreadTune", "%s setschedparam rr prio=%d failed rc=%d", role, rr_prio, rc);
        }
        return;
    }

    const int nice_val = rflow::common::util::ReadEnvIntInRange("RFLOW_MEDIA_THREAD_NICE", -8, -20, 19);
    if (setpriority(PRIO_PROCESS, 0, nice_val) != 0) {
        RFLOW_LOG_TAG_E("ThreadTune", "%s setpriority nice=%d failed errno=%d", role, nice_val, errno);
    }
}

int64_t DecodeQueueStaleDropBudgetUs(int requested_fps) {
    const int wait_ms = capture_policy::MjpegDecodeQueueMaxWaitMsForFps(requested_fps);
    return static_cast<int64_t>(wait_ms) * 1000;
}

void LogMjpegDecodeTiming(const char* tag, int64_t before_us, int64_t after_us) {
    static std::atomic<unsigned> g_n{0};
    const unsigned n = ++g_n;
    if ((n % 30) != 0) {
        return;
    }
    const double ms = static_cast<double>(after_us - before_us) / 1000.0;
    RFLOW_LOG_TAG_I("MJPEG_DECODE", "[%s] frame#%u before_us=%lld after_us=%lld duration_ms=%f", tag,
                    static_cast<unsigned>(n), static_cast<long long>(before_us), static_cast<long long>(after_us), ms);

}

#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
class Nv12PoolLease {
 public:
  Nv12PoolLease(std::function<void()> on_release, webrtc::scoped_refptr<webrtc::NV12Buffer> buf)
      : on_release_(std::move(on_release)), buf_(std::move(buf)) {}
  ~Nv12PoolLease() {
    if (on_release_) {
      on_release_();
    }
  }
  webrtc::scoped_refptr<webrtc::NV12Buffer> buffer() const { return buf_; }

 private:
  std::function<void()> on_release_;
  webrtc::scoped_refptr<webrtc::NV12Buffer> buf_;
};

class PooledNv12Buffer : public webrtc::NV12BufferInterface {
 public:
  static webrtc::scoped_refptr<PooledNv12Buffer> Create(std::shared_ptr<Nv12PoolLease> lease) {
    return webrtc::make_ref_counted<PooledNv12Buffer>(std::move(lease));
  }
  explicit PooledNv12Buffer(std::shared_ptr<Nv12PoolLease> lease)
      : lease_(std::move(lease)), backing_(lease_->buffer()) {}

  Type type() const override { return Type::kNV12; }
  int width() const override { return backing_->width(); }
  int height() const override { return backing_->height(); }
  int StrideY() const override { return backing_->StrideY(); }
  int StrideUV() const override { return backing_->StrideUV(); }
  const uint8_t* DataY() const override { return backing_->DataY(); }
  const uint8_t* DataUV() const override { return backing_->DataUV(); }
  webrtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override { return backing_->ToI420(); }

  webrtc::NV12Buffer* mutable_backing() const { return backing_.get(); }

 private:
  std::shared_ptr<Nv12PoolLease> lease_;
  webrtc::scoped_refptr<webrtc::NV12Buffer> backing_;
};
#endif

// V4L2 帧间隔：interval = numerator/denominator 秒；fps = denominator/numerator。
// 判断是否可达 min_fps（含）：denominator >= min_fps * numerator。
bool IntervalRatioAtLeastFps(const struct v4l2_fract& interval, int min_fps) {
    if (interval.numerator == 0 || min_fps <= 0) {
        return false;
    }
    return static_cast<uint64_t>(interval.denominator) >=
           static_cast<uint64_t>(min_fps) * static_cast<uint64_t>(interval.numerator);
}

// VIDIOC_ENUM_FRAMEINTERVALS：判断 pixfmt@WxH 是否报告存在 ≥ min_fps 的帧率档位。
bool PixelFormatSupportsMinCaptureFps(int fd, uint32_t pixfmt, int w, int h, int min_fps) {
    if (fd < 0 || w <= 0 || h <= 0 || min_fps <= 0) {
        return false;
    }
    struct v4l2_frmivalenum fie {};
    fie.pixel_format = pixfmt;
    fie.width        = static_cast<__u32>(w);
    fie.height       = static_cast<__u32>(h);
    for (fie.index = 0;; ++fie.index) {
        if (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fie) != 0) {
            return false;
        }
        if (fie.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            if (IntervalRatioAtLeastFps(fie.discrete, min_fps)) {
                return true;
            }
        } else if (fie.type == V4L2_FRMIVAL_TYPE_STEPWISE || fie.type == V4L2_FRMIVAL_TYPE_CONTINUOUS) {
            return IntervalRatioAtLeastFps(fie.stepwise.min, min_fps);
        } else {
            return false;
        }
    }
}

}  // anonymous namespace inside WEBRTC_LINUX

#endif  // WEBRTC_LINUX && __linux__

CameraVideoTrackSource::CameraVideoTrackSource() : webrtc::AdaptedVideoTrackSource() {}

CameraVideoTrackSource::~CameraVideoTrackSource() {
    Stop();
}

bool CameraVideoTrackSource::GetNegotiatedCaptureSize(int* width, int* height) const {
#if defined(WEBRTC_LINUX) && defined(__linux__)
    if (width && height && direct_cap_w_ > 0 && direct_cap_h_ > 0) {
        *width = direct_cap_w_;
        *height = direct_cap_h_;
        return true;
    }
#else
    (void)width;
    (void)height;
#endif
    return false;
}

bool CameraVideoTrackSource::GetNegotiatedCaptureFramerate(int* out_fps) const {
    if (!out_fps || negotiated_capture_fps_ <= 0) {
        return false;
    }
    *out_fps = negotiated_capture_fps_;
    return true;
}

#if defined(WEBRTC_LINUX) && defined(__linux__)

void CameraVideoTrackSource::StopDirectV4l2() {
    direct_run_ = false;
    if (direct_thread_.joinable()) {
        direct_thread_.join();
    }
    {
        std::lock_guard<std::mutex> lk(jpeg_queue_mu_);
        decode_worker_exit_ = true;
    }
    jpeg_queue_cv_.notify_all();
    if (decode_thread_.joinable()) {
        decode_thread_.join();
    }
    {
        std::deque<MjpegPendingBuf> leftover;
        {
            std::lock_guard<std::mutex> lk(jpeg_queue_mu_);
            leftover.swap(jpeg_queue_);
        }
        for (const MjpegPendingBuf& drop : leftover) {
            QBufV4l2Index(drop.index);
        }
    }
    decode_worker_exit_ = false;
    nv12_pool_.clear();
    nv12_slot_in_use_.reset();
    nv12_slot_count_ = 0;
    nv12_pool_w_ = nv12_pool_h_ = 0;
    nv12_ring_next_ = 0;
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
    mjpeg_mpp_.reset();
#endif
    if (direct_fd_ >= 0) {
        enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(direct_fd_, VIDIOC_STREAMOFF, &t);
    }
    for (size_t i = 0; i < direct_mmap_.size(); ++i) {
        if (direct_mmap_[i] && direct_mmap_len_[i] > 0) {
            munmap(direct_mmap_[i], direct_mmap_len_[i]);
        }
    }
    for (int fd : direct_expbuf_fd_) {
        if (fd >= 0) {
            close(fd);
        }
    }
    direct_expbuf_fd_.clear();
    direct_mmap_.clear();
    direct_mmap_len_.clear();
    if (direct_fd_ >= 0) {
        close(direct_fd_);
        direct_fd_ = -1;
    }
    direct_cap_w_ = direct_cap_h_ = 0;
    direct_pixfmt_ = 0;
}

void CameraVideoTrackSource::ApplyMjpegPipelineOptions(const V4l2MjpegPipelineOptions* p) {
    V4l2MjpegPipelineOptions def;
    const V4l2MjpegPipelineOptions& o = p ? *p : def;
    mjpeg_queue_latest_only_ = o.mjpeg_queue_latest_only;
    int qmax = o.mjpeg_queue_max;
    if (qmax < 1) {
        qmax = 1;
    }
    if (qmax > 32) {
        qmax = 32;
    }
    mjpeg_queue_max_ = static_cast<size_t>(qmax);
    int slots = o.nv12_pool_slots;
    if (slots < 4) {
        slots = 4;
    }
    if (slots > 16) {
        slots = 16;
    }
    nv12_pool_slots_ = slots;

    int nbuf = o.v4l2_buffer_count;
    if (nbuf < 2) {
        nbuf = 2;
    }
    if (nbuf > 32) {
        nbuf = 32;
    }
    v4l2_buffer_count_ = nbuf;

    int pto = o.v4l2_poll_timeout_ms;
    if (pto < 1) {
        pto = 1;
    }
    if (pto > 2000) {
        pto = 2000;
    }
    v4l2_poll_timeout_ms_ = pto;

    mjpeg_decode_inline_ = o.mjpeg_decode_inline;
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
    v4l2_ext_dma_config_ = o.mjpeg_v4l2_ext_dma;
    mjpeg_rga_config_ = o.mjpeg_rga_to_mpp;
#endif
    if (const char* e = std::getenv("RFLOW_MJPEG_DECODE_INLINE")) {
        const char c = e[0];
        if (c == '1' || c == 'y' || c == 'Y' || c == 't' || c == 'T') {
            mjpeg_decode_inline_ = true;
        }
        if (c == '0' || c == 'n' || c == 'N' || c == 'f' || c == 'F') {
            mjpeg_decode_inline_ = false;
        }
    }

    // 延迟 QBUF 时，队列里每帧占一块 mmap；须留至少 1 块在驱动里供采集写入。
    const size_t qcap = static_cast<size_t>(std::max(1, v4l2_buffer_count_ - 1));
    if (mjpeg_queue_max_ > qcap) {
        RTC_LOG(LS_INFO) << "[CameraV4L2] mjpeg_queue_max clamped to " << qcap
                         << " (v4l2_buffer_count=" << v4l2_buffer_count_ << ")";
        mjpeg_queue_max_ = qcap;
    }
}

void CameraVideoTrackSource::QBufV4l2Index(unsigned int index) {
    if (direct_fd_ < 0) {
        return;
    }
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
    if (index < direct_expbuf_fd_.size()) {
        const int exp_fd = direct_expbuf_fd_[index];
        if (exp_fd >= 0) {
            struct dma_buf_sync sync {};
            sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
            if (ioctl(exp_fd, DMA_BUF_IOCTL_SYNC, &sync) < 0) {
                RFLOW_LOG_TAG_W("CameraV4L2", "EXPBUF DMA_BUF_SYNC_END index=%u errno=%d", index, errno);
            }
        }
    }
#endif
    struct v4l2_buffer buf {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    if (ioctl(direct_fd_, VIDIOC_QBUF, &buf) < 0) {
        RTC_LOG(LS_WARNING) << "[CameraV4L2] VIDIOC_QBUF index=" << index << " errno=" << errno;
    }
}

void CameraVideoTrackSource::EnsureNv12Pool(int w, int h) {
    const int slots = nv12_pool_slots_;
    if (w <= 0 || h <= 0 || slots < 4) {
        return;
    }
    if (nv12_pool_.size() == static_cast<size_t>(slots) && nv12_pool_w_ == w && nv12_pool_h_ == h &&
        nv12_slot_count_ == static_cast<size_t>(slots)) {
        return;
    }
    nv12_pool_.clear();
    nv12_slot_in_use_.reset();
    nv12_slot_count_ = 0;
    nv12_pool_.reserve(static_cast<size_t>(slots));
    for (int i = 0; i < slots; ++i) {
        webrtc::scoped_refptr<webrtc::NV12Buffer> b = webrtc::NV12Buffer::Create(w, h);
        if (!b) {
            nv12_pool_.clear();
            nv12_slot_in_use_.reset();
            nv12_slot_count_ = 0;
            nv12_pool_w_ = nv12_pool_h_ = 0;
            return;
        }
        nv12_pool_.push_back(std::move(b));
    }
    nv12_slot_in_use_ = std::make_unique<std::atomic<bool>[]>(nv12_pool_.size());
    nv12_slot_count_ = nv12_pool_.size();
    for (size_t i = 0; i < nv12_slot_count_; ++i) {
        nv12_slot_in_use_[i].store(false, std::memory_order_relaxed);
    }
    nv12_pool_w_ = w;
    nv12_pool_h_ = h;
    nv12_ring_next_ = 0;
}

void CameraVideoTrackSource::ReleaseNv12PoolSlot(size_t slot_index) {
    if (nv12_slot_in_use_ && slot_index < nv12_slot_count_) {
        nv12_slot_in_use_[slot_index].store(false, std::memory_order_release);
    }
}

webrtc::scoped_refptr<webrtc::VideoFrameBuffer> CameraVideoTrackSource::AcquireNv12PoolBuffer(int w, int h) {
    EnsureNv12Pool(w, h);
    if (nv12_pool_.empty() || !nv12_slot_in_use_ || nv12_slot_count_ != nv12_pool_.size()) {
        return nullptr;
    }
    const size_t n = nv12_pool_.size();
    for (size_t tried = 0; tried < n; ++tried) {
        const size_t idx = (nv12_ring_next_ + tried) % n;
        bool expected = false;
        if (!nv12_slot_in_use_[idx].compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            continue;
        }
        nv12_ring_next_ = idx + 1;
        auto lease = std::make_shared<Nv12PoolLease>(
            [this, idx]() { ReleaseNv12PoolSlot(idx); }, nv12_pool_[idx]);
        return PooledNv12Buffer::Create(std::move(lease));
    }
    static std::atomic<uint64_t> drop_count{0};
    ++drop_count;
    nv12_pool_busy_count_.fetch_add(1, std::memory_order_relaxed);
    return nullptr;
}

bool CameraVideoTrackSource::StartDirectV4l2(const char* device_path, int width, int height, int fps) {
    StopDirectV4l2();

    // 建流前用阻塞 open；刚结束的上一次采集或其它进程可能短暂占设备，EBUSY 时重试。
    direct_fd_ = -1;
    for (int attempt = 0; attempt < 20; ++attempt) {
        direct_fd_ = open(device_path, O_RDWR, 0);
        if (direct_fd_ >= 0) {
            break;
        }
        if (errno != EBUSY) {
            RFLOW_LOG_TAG_E("CameraV4L2", "open %s failed errno=%d", device_path, errno);
            return false;
        }
        usleep(200 * 1000);
    }
    if (direct_fd_ < 0) {
        RFLOW_LOG_TAG_E("CameraV4L2", "open %s failed errno=EBUSY after retries", device_path);
        return false;
    }

    struct v4l2_capability cap {};
    if (ioctl(direct_fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        RFLOW_LOG_TAG_E("CameraV4L2", "VIDIOC_QUERYCAP errno=%d", errno);
        close(direct_fd_);
        direct_fd_ = -1;
        return false;
    }
    if (!(cap.device_caps & V4L2_CAP_VIDEO_CAPTURE)) {
        RFLOW_LOG_TAG_E("CameraV4L2", "not a VIDEO_CAPTURE node: %s", device_path);
        close(direct_fd_);
        direct_fd_ = -1;
        return false;
    }

    // UVC 等设备在已有可用格式时，STREAMOFF/REQBUFS(0)/S_FMT 可能一律 EBUSY；先读当前格式并尽量沿用。
    auto accept_gfmt_if_usable = [&]() -> bool {
        struct v4l2_format fmt {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(direct_fd_, VIDIOC_G_FMT, &fmt) < 0) {
            return false;
        }
        const uint32_t pf = fmt.fmt.pix.pixelformat;
        if (pf != V4L2_PIX_FMT_MJPEG && pf != V4L2_PIX_FMT_YUYV) {
            return false;
        }
        if (fmt.fmt.pix.width < 160 || fmt.fmt.pix.height < 120) {
            return false;
        }
        direct_cap_w_ = static_cast<int>(fmt.fmt.pix.width);
        direct_cap_h_ = static_cast<int>(fmt.fmt.pix.height);
        direct_pixfmt_ = pf;
        return true;
    };

    auto try_sfmt = [&](uint32_t pixfmt, int w, int h) -> bool {
        struct v4l2_format fmt {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = static_cast<__u32>(w);
        fmt.fmt.pix.height = static_cast<__u32>(h);
        fmt.fmt.pix.pixelformat = pixfmt;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;
        fmt.fmt.pix.sizeimage = 0;
        for (int attempt = 0; attempt < 12; ++attempt) {
            if (ioctl(direct_fd_, VIDIOC_S_FMT, &fmt) == 0) {
                direct_cap_w_ = static_cast<int>(fmt.fmt.pix.width);
                direct_cap_h_ = static_cast<int>(fmt.fmt.pix.height);
                direct_pixfmt_ = fmt.fmt.pix.pixelformat;
                return direct_cap_w_ > 0 && direct_cap_h_ > 0;
            }
            if (errno != EBUSY) {
                return false;
            }
            usleep(150 * 1000);
        }
        return false;
    };

    // 保留驱动当前分辨率，只改像素格式（部分 ISP 节点拒绝任意 WxH）
    auto try_sfmt_keep_size = [&](uint32_t pixfmt) -> bool {
        struct v4l2_format fmt {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(direct_fd_, VIDIOC_G_FMT, &fmt) < 0) {
            return false;
        }
        fmt.fmt.pix.pixelformat = pixfmt;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;
        fmt.fmt.pix.sizeimage = 0;
        for (int attempt = 0; attempt < 12; ++attempt) {
            if (ioctl(direct_fd_, VIDIOC_S_FMT, &fmt) == 0) {
                direct_cap_w_ = static_cast<int>(fmt.fmt.pix.width);
                direct_cap_h_ = static_cast<int>(fmt.fmt.pix.height);
                direct_pixfmt_ = fmt.fmt.pix.pixelformat;
                return direct_cap_w_ > 0 && direct_cap_h_ > 0;
            }
            if (errno != EBUSY) {
                return false;
            }
            usleep(150 * 1000);
        }
        return false;
    };

    const uint32_t yuyv = V4L2_PIX_FMT_YUYV;
    const uint32_t mjpeg = V4L2_PIX_FMT_MJPEG;

    // 与推流请求的 WIDTH/HEIGHT 一致：先 S_FMT 请求目标分辨率，禁止一上来 G_FMT 沿用 1080p 导致与配置不符。
    auto try_sfmt_exact = [&](uint32_t pixfmt, int w, int h) -> bool {
        if (!try_sfmt(pixfmt, w, h)) {
            return false;
        }
        return direct_cap_w_ == w && direct_cap_h_ == h;
    };

    // 同分辨率：ENUM_FRAMEINTERVALS 若报告 YUYV@WxH 存在 ≥ fps_need 的档位（≥30 且不低于请求的 fps），则优先 YUYV；
    // 否则优先 MJPEG。枚举失败视为 YUYV 不足帧率 → 先试 MJPEG。
    const int  fps_need          = std::max(30, fps > 0 ? fps : 30);
    const bool yuyv_fast_enough = PixelFormatSupportsMinCaptureFps(direct_fd_, yuyv, width, height, fps_need);
    bool prefer_mjpeg_pixfmt    = !yuyv_fast_enough;
    if (yuyv_fast_enough) {
        RFLOW_LOG_TAG_I("CameraV4L2", "prefer YUYV first at %dx%d (driver reports ≥%dfps)", width, height, fps_need);
    } else {
        RFLOW_LOG_TAG_I(
            "CameraV4L2",
            "prefer MJPEG first at %dx%d (YUYV not reported ≥%dfps via ENUM_FRAMEINTERVALS)", width, height,
            fps_need);
    }
    bool fmt_ok = prefer_mjpeg_pixfmt
                      ? (try_sfmt_exact(mjpeg, width, height) || try_sfmt_exact(yuyv, width, height))
                      : (try_sfmt_exact(yuyv, width, height) || try_sfmt_exact(mjpeg, width, height));

    // 设备已是目标分辨率但 S_FMT(改分辨率) 失败时，只切像素格式或直接使用当前帧格式。
    if (!fmt_ok) {
        struct v4l2_format g {};
        g.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(direct_fd_, VIDIOC_G_FMT, &g) == 0 && static_cast<int>(g.fmt.pix.width) == width &&
            static_cast<int>(g.fmt.pix.height) == height) {
            const uint32_t pf = g.fmt.pix.pixelformat;
            if (pf == mjpeg || pf == yuyv) {
                direct_cap_w_ = width;
                direct_cap_h_ = height;
                direct_pixfmt_ = pf;
                fmt_ok = true;
            } else {
                fmt_ok = prefer_mjpeg_pixfmt ? (try_sfmt_keep_size(mjpeg) || try_sfmt_keep_size(yuyv))
                                             : (try_sfmt_keep_size(yuyv) || try_sfmt_keep_size(mjpeg));
                if (fmt_ok && (direct_cap_w_ != width || direct_cap_h_ != height)) {
                    fmt_ok = false;
                }
            }
        }
    }

    // 最后：可读且尺寸与配置一致才接受（避免 EBUSY 时默默用错误分辨率）。
    if (!fmt_ok && accept_gfmt_if_usable()) {
        if (direct_cap_w_ == width && direct_cap_h_ == height) {
            fmt_ok = true;
        } else {
            RFLOW_LOG_TAG_E(
                "CameraV4L2",
                "need capture %dx%d per config, but device is %dx%d (VIDIOC_S_FMT unavailable or EBUSY). Match "
                "WIDTH/HEIGHT to the device or release the camera.",
                width, height, direct_cap_w_, direct_cap_h_);
            close(direct_fd_);
            direct_fd_ = -1;
            return false;
        }
    }

    if (!fmt_ok) {
        struct v4l2_format g {};
        g.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        int e = errno;
        if (ioctl(direct_fd_, VIDIOC_G_FMT, &g) == 0) {
            RFLOW_LOG_TAG_E("CameraV4L2",
                            "cannot set capture %dx%d (device reports %ux%u) errno=%d (%s)", width, height,
                            static_cast<unsigned>(g.fmt.pix.width), static_cast<unsigned>(g.fmt.pix.height), e,
                            strerror(e));
        } else {
            RFLOW_LOG_TAG_E("CameraV4L2", "VIDIOC_S_FMT failed for MJPEG/YUYV errno=%d (%s)", e, strerror(e));
        }
        close(direct_fd_);
        direct_fd_ = -1;
        return false;
    }

    struct v4l2_streamparm parm {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(direct_fd_, VIDIOC_G_PARM, &parm) == 0 &&
        (parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator =
            static_cast<__u32>(fps > 0 ? fps : 30);
        ioctl(direct_fd_, VIDIOC_S_PARM, &parm);
    }

    struct v4l2_requestbuffers rb {};
    rb.count = static_cast<unsigned int>(v4l2_buffer_count_);
    rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rb.memory = V4L2_MEMORY_MMAP;
    if (ioctl(direct_fd_, VIDIOC_REQBUFS, &rb) < 0 || rb.count < 2) {
        RFLOW_LOG_TAG_E("CameraV4L2", "VIDIOC_REQBUFS failed errno=%d", errno);
        close(direct_fd_);
        direct_fd_ = -1;
        return false;
    }

    const unsigned int nbuf = rb.count;
    direct_mmap_.resize(nbuf, nullptr);
    direct_mmap_len_.resize(nbuf, 0);
    direct_expbuf_fd_.assign(nbuf, -1);
    for (unsigned int i = 0; i < nbuf; ++i) {
        struct v4l2_buffer buf {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(direct_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            StopDirectV4l2();
            return false;
        }
        void* p = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, direct_fd_, buf.m.offset);
        if (p == MAP_FAILED) {
            StopDirectV4l2();
            return false;
        }
        direct_mmap_[i] = p;
        direct_mmap_len_[i] = buf.length;
        if (ioctl(direct_fd_, VIDIOC_QBUF, &buf) < 0) {
            StopDirectV4l2();
            return false;
        }
    }

#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
    {
        const auto zc_policy = rflow::service::impl::policy::EvaluateMjpegZeroCopyPolicy(
            v4l2_ext_dma_config_, mjpeg_rga_config_);
    if (zc_policy.use_v4l2_ext_dmabuf || zc_policy.use_rga_to_mpp) {
        unsigned exp_ok = 0;
        for (unsigned int i = 0; i < nbuf; ++i) {
            struct v4l2_exportbuffer exp {};
            exp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            exp.index = i;
            exp.plane = 0;
            exp.flags = O_CLOEXEC;
            if (ioctl(direct_fd_, VIDIOC_EXPBUF, &exp) == 0 && exp.fd >= 0) {
                direct_expbuf_fd_[i] = exp.fd;
                ++exp_ok;
            }
        }
        if (exp_ok == nbuf) {
            if (zc_policy.use_v4l2_ext_dmabuf) {
                RFLOW_LOG_TAG_I("CameraV4L2", "VIDIOC_EXPBUF: %u dma-buf fd(s) → MPP JPEG EXT_DMA import", nbuf);
            } else {
                RFLOW_LOG_TAG_I("CameraV4L2",
                                "VIDIOC_EXPBUF: %u dma-buf fd(s) -> RGA copy to MPP input",
                                nbuf);
            }
        } else if (exp_ok > 0) {
            RFLOW_LOG_TAG_I("CameraV4L2", "VIDIOC_EXPBUF: %u/%u (per-buffer dma or memcpy)", exp_ok, nbuf);
        } else {
            RFLOW_LOG_TAG_I("CameraV4L2", "VIDIOC_EXPBUF unsupported; MPP JPEG uses memcpy from mmap");
        }
    }
    }  // zc_policy scope
#endif

    enum v4l2_buf_type typ = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(direct_fd_, VIDIOC_STREAMON, &typ) < 0) {
        RFLOW_LOG_TAG_E("CameraV4L2", "VIDIOC_STREAMON errno=%d", errno);
        StopDirectV4l2();
        return false;
    }

    // 驱动在 S_PARM/STREAMON 之后给出的实际帧间隔：fps ≈ denominator / numerator（V4L2 文档 timeperframe）。
    negotiated_capture_fps_ = (fps > 0 ? fps : 30);
    {
        struct v4l2_streamparm gparm {};
        gparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(direct_fd_, VIDIOC_G_PARM, &gparm) == 0 &&
            (gparm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
            const __u32 n = gparm.parm.capture.timeperframe.numerator;
            const __u32 d = gparm.parm.capture.timeperframe.denominator;
            if (n > 0 && d > 0) {
                const int calc = static_cast<int>((static_cast<uint64_t>(d) + n / 2) / n);
                if (calc >= 1 && calc <= 480) {
                    negotiated_capture_fps_ = calc;
                }
            }
        }
    }

    {
        int fl = fcntl(direct_fd_, F_GETFL, 0);
        if (fl >= 0) {
            fcntl(direct_fd_, F_SETFL, fl | O_NONBLOCK);
        }
    }

    direct_run_ = true;
    decode_worker_exit_ = false;
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
    if (prefer_mpp_mjpeg_decode_ && direct_pixfmt_ == V4L2_PIX_FMT_MJPEG) {
        auto dec = std::make_shared<rflow::rtc::hw::rockchip_mpp::RkMppMjpegDecoder>();
        if (dec->Init()) {
            dec->SetPipelineV4l2ExtDmabuf(v4l2_ext_dma_config_);
            dec->SetPipelineRgaToMpp(mjpeg_rga_config_);
            const int pool_slack =
                rflow::common::util::ReadEnvIntInRange("RFLOW_MJPEG_DEC_OUT_POOL_SLACK", 2, 0, 8);
            const int pool_max = v4l2_buffer_count_ + static_cast<int>(mjpeg_queue_max_) + pool_slack;
            dec->SetOutputBufferPoolLimit(pool_max, direct_cap_w_, direct_cap_h_);
            mjpeg_mpp_ = dec;
            RFLOW_LOG_TAG_I(
                "CameraV4L2",
                "MJPEG: Rockchip MPP decode -> NV12 (zero I420/libyuv chroma conversion in HW encode path) "
                "dec_out_pool=%d",
                pool_max);
        }
    }
#endif
    const bool mjpeg_async =
        (direct_pixfmt_ == static_cast<uint32_t>(V4L2_PIX_FMT_MJPEG)) && !mjpeg_decode_inline_;
    if (mjpeg_async) {
        decode_thread_ = std::thread([this]() { DecodeWorkerThreadMain(); });
    }
    direct_thread_ = std::thread([this]() { DirectCaptureThreadMain(); });
    std::ostringstream cap_line;
    cap_line << "Direct capture " << device_path << " " << direct_cap_w_ << "x" << direct_cap_h_
             << " @" << negotiated_capture_fps_ << "fps fourcc=0x" << std::hex << direct_pixfmt_ << std::dec
             << " mmap_bufs=" << nbuf << " poll_timeout_ms=" << v4l2_poll_timeout_ms_;
    if (direct_pixfmt_ == static_cast<uint32_t>(V4L2_PIX_FMT_MJPEG)) {
        if (mjpeg_decode_inline_) {
            cap_line << " mjpeg_inline_decode=1";
        } else {
            cap_line << " mjpeg_deferred_qbuf=1";
        }
    }
    RFLOW_LOG_TAG_I("CameraV4L2", "%s", cap_line.str().c_str());
    return true;
}

void CameraVideoTrackSource::DecodeWorkerThreadMain() {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), "wrtc_mjpg_dec");
    ApplyThreadTuneIfRequested("mjpeg_decode", "RFLOW_MJPEG_DECODE_CPU", requested_capture_fps_);
#endif
    while (true) {
        MjpegPendingBuf job{};
        bool have_job = false;
        bool shutdown_skip_decode = false;
        {
            std::unique_lock<std::mutex> lk(jpeg_queue_mu_);
            jpeg_queue_cv_.wait(lk, [this] { return decode_worker_exit_ || !jpeg_queue_.empty(); });
            if (!jpeg_queue_.empty()) {
                job = jpeg_queue_.front();
                jpeg_queue_.pop_front();
                have_job = true;
                shutdown_skip_decode = decode_worker_exit_;
            } else if (decode_worker_exit_) {
                break;
            }
        }
        if (!have_job) {
            continue;
        }
        if (!shutdown_skip_decode && job.index < direct_mmap_.size() && direct_mmap_[job.index] &&
            job.bytesused > 0) {
            const uint8_t* src = static_cast<const uint8_t*>(direct_mmap_[job.index]);
            const int64_t decode_queue_wait_us =
                (job.enqueue_time_us > 0) ? (webrtc::TimeMicros() - job.enqueue_time_us) : 0;
            ProcessV4l2CapturedFrame(job.index, src, job.bytesused, job.dq_time_us, job.v4l2_timestamp_us,
                                     job.poll_wait_us, job.dqbuf_ioctl_us, decode_queue_wait_us);
        }
        QBufV4l2Index(job.index);
    }
}

void CameraVideoTrackSource::ProcessV4l2CapturedFrame(unsigned int buf_index,
                                                      const uint8_t* src,
                                                      size_t bytesused,
                                                      int64_t dq_time_us,
                                                      int64_t v4l2_timestamp_us,
                                                      int64_t poll_wait_us,
                                                      int64_t dqbuf_ioctl_us,
                                                      int64_t decode_queue_wait_us) {
    if (!src || bytesused == 0) {
        return;
    }
    // 供 RFLOW_MJPEG_TO_H264_TRACE：与编码器内 TimeMicros 差值 = 从进入本函数（MJPEG 已在 mmap）到 H264 输出的大致链路耗时（含解码、VSE 排队、编码）。
    const int64_t pipeline_t0_us = webrtc::TimeMicros();
    const int w = direct_cap_w_;
    const int h = direct_cap_h_;
    bool ok = false;
#if defined(RFLOW_HAVE_ROCKCHIP_MPP)
    int dma_fd = -1;
    size_t dma_cap = 0;
    if (buf_index < direct_expbuf_fd_.size() && buf_index < direct_mmap_len_.size()) {
        dma_fd = direct_expbuf_fd_[buf_index];
        dma_cap = direct_mmap_len_[buf_index];
    }
    const auto zc_policy = rflow::service::impl::policy::EvaluateMjpegZeroCopyPolicy(
        v4l2_ext_dma_config_, mjpeg_rga_config_);
    const bool mpp_jpeg_dma =
        (dma_fd >= 0 && dma_cap >= bytesused &&
         (zc_policy.use_v4l2_ext_dmabuf || zc_policy.use_rga_to_mpp));
    const int dma_arg_fd = mpp_jpeg_dma ? dma_fd : -1;
    const size_t dma_arg_cap = mpp_jpeg_dma ? dma_cap : 0;
    if (mjpeg_mpp_ && direct_pixfmt_ == static_cast<uint32_t>(V4L2_PIX_FMT_MJPEG)) {
        if (zc_policy.prefer_native_zero_copy_to_enc) {
            webrtc::scoped_refptr<rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer> native;
            // 始终传 mmap 指针：RGA 失败时会 memcpy 回退；EXT_DMA 成功时解码器忽略指针。
            const bool dec_native =
                mjpeg_mpp_->DecodeJpegToNativeDecFrame(src, bytesused, w, h, &native, dma_arg_fd, dma_arg_cap,
                                                       dq_time_us, v4l2_timestamp_us, poll_wait_us, dqbuf_ioctl_us,
                                                       decode_queue_wait_us, mjpeg_mpp_);
            if (dec_native && native) {
                webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                               .set_video_frame_buffer(native)
                                               .set_timestamp_us(pipeline_t0_us)
                                               .set_rotation(webrtc::kVideoRotation_0)
                                               .build();
                OnFrame(frame);
                return;
            }
        }
        EnsureNv12Pool(w, h);
        {
            webrtc::scoped_refptr<webrtc::VideoFrameBuffer> pool_vfb = AcquireNv12PoolBuffer(w, h);
            PooledNv12Buffer* pooled = pool_vfb ? static_cast<PooledNv12Buffer*>(pool_vfb.get()) : nullptr;
            webrtc::NV12Buffer* nv12 = pooled ? pooled->mutable_backing() : nullptr;
            if (nv12) {
                const int64_t decode_before_us = webrtc::TimeMicros();
                const bool dec_ok =
                    mjpeg_mpp_->DecodeJpegToNV12(src, bytesused, w, h, nv12, dma_arg_fd, dma_arg_cap);
                const int64_t decode_after_us = webrtc::TimeMicros();
                if (dec_ok) {
                    LogMjpegDecodeTiming("mpp-nv12-pool", decode_before_us, decode_after_us);
                    webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                                   .set_video_frame_buffer(pool_vfb)
                                                   .set_timestamp_us(pipeline_t0_us)
                                                   .set_rotation(webrtc::kVideoRotation_0)
                                                   .build();
                    OnFrame(frame);
                    return;
                }
            }
        }
        {
            webrtc::scoped_refptr<webrtc::NV12Buffer> nv12 = webrtc::NV12Buffer::Create(w, h);
            const int64_t decode_before_us = webrtc::TimeMicros();
            const bool dec_ok =
                nv12 && mjpeg_mpp_->DecodeJpegToNV12(src, bytesused, w, h, nv12.get(), dma_arg_fd, dma_arg_cap);
            const int64_t decode_after_us = webrtc::TimeMicros();
            if (dec_ok) {
                LogMjpegDecodeTiming("mpp-nv12-alloc", decode_before_us, decode_after_us);
                webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                               .set_video_frame_buffer(nv12)
                                               .set_timestamp_us(pipeline_t0_us)
                                               .set_rotation(webrtc::kVideoRotation_0)
                                               .build();
                OnFrame(frame);
                return;
            }
        }
    }
#endif
    webrtc::scoped_refptr<webrtc::I420Buffer> i420 = webrtc::I420Buffer::Create(w, h);
    const bool log_libyuv_mjpeg =
        (direct_pixfmt_ == static_cast<uint32_t>(V4L2_PIX_FMT_MJPEG));
    int64_t decode_before_us = 0;
    if (log_libyuv_mjpeg) {
        decode_before_us = webrtc::TimeMicros();
    }
    int conv_ret = 0;
    {
        const webrtc::VideoType vtype = (direct_pixfmt_ == static_cast<uint32_t>(V4L2_PIX_FMT_MJPEG))
                                              ? webrtc::VideoType::kMJPEG
                                              : webrtc::VideoType::kYUY2;
        conv_ret = libyuv::ConvertToI420(src, bytesused, i420->MutableDataY(), i420->StrideY(), i420->MutableDataU(),
                                         i420->StrideU(), i420->MutableDataV(), i420->StrideV(), 0, 0, w, h, w, h,
                                         libyuv::kRotate0, webrtc::ConvertVideoType(vtype));
        int conv = conv_ret;
        if (conv != 0 && direct_pixfmt_ == static_cast<uint32_t>(V4L2_PIX_FMT_MJPEG)) {
            conv = libyuv::MJPGToI420(src, bytesused, i420->MutableDataY(), i420->StrideY(), i420->MutableDataU(),
                                      i420->StrideU(), i420->MutableDataV(), i420->StrideV(), w, h, w, h);
        }
        ok = (conv == 0);
    }
    if (!ok) {
        const uint64_t total = convert_fail_drop_count_.fetch_add(1, std::memory_order_relaxed) + 1;
        char detail[96];
        snprintf(detail, sizeof(detail), "pixfmt=0x%x conv_err=%d size=%dx%d",
                 static_cast<unsigned>(direct_pixfmt_), conv_ret, w, h);
        rflow::core::rtc::PushPipelineDropStats::Instance().LogDrop("Capture/Convert", 1, total, detail);
    }
    if (ok && log_libyuv_mjpeg) {
        LogMjpegDecodeTiming("libyuv-i420", decode_before_us, webrtc::TimeMicros());
    }
    if (ok) {
        webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                       .set_video_frame_buffer(i420)
                                       .set_timestamp_us(pipeline_t0_us)
                                       .set_rotation(webrtc::kVideoRotation_0)
                                       .build();
        OnFrame(frame);
    }
}

void CameraVideoTrackSource::DirectCaptureThreadMain() {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), "wrtc_v4l2_cap");
    ApplyThreadTuneIfRequested("v4l2_capture", "RFLOW_V4L2_CAPTURE_CPU", requested_capture_fps_);
#endif
    std::atomic<unsigned> poll_log_counter{0};
    while (direct_run_.load(std::memory_order_relaxed)) {
        struct pollfd pfd {};
        pfd.fd = direct_fd_;
        pfd.events = POLLIN;
        const auto poll_t0 = std::chrono::steady_clock::now();
        int pr = poll(&pfd, 1, v4l2_poll_timeout_ms_);
        const int64_t poll_wait_us = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - poll_t0).count());
        if (LatencyTraceEnabled() && pr > 0) {
            const auto poll_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - poll_t0)
                                     .count();
            const unsigned n = ++poll_log_counter;
            if ((n % 90u) == 0u && poll_ms > 3.0) {
                RFLOW_LOG_TAG_I("Latency", "V4L2 poll→readable wait_ms=%f (frame#%u)", poll_ms,
                                static_cast<unsigned>(n));
            }
        }
        if (pr <= 0) {
            continue;
        }
        struct v4l2_buffer buf {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        const int64_t dq_ioctl_t0_us = webrtc::TimeMicros();
        if (ioctl(direct_fd_, VIDIOC_DQBUF, &buf) < 0) {
            continue;
        }
        const int64_t dq_time_us = webrtc::TimeMicros();
        const int64_t dqbuf_ioctl_us = dq_time_us - dq_ioctl_t0_us;
        const int64_t v4l2_timestamp_us =
            static_cast<int64_t>(buf.timestamp.tv_sec) * webrtc::kNumMicrosecsPerSec + buf.timestamp.tv_usec;
        if (buf.index >= direct_mmap_.size() || !direct_mmap_[buf.index]) {
            QBufV4l2Index(buf.index);
            continue;
        }
        const uint8_t* src = static_cast<const uint8_t*>(direct_mmap_[buf.index]);
        if (direct_pixfmt_ == static_cast<uint32_t>(V4L2_PIX_FMT_MJPEG)) {
            if (buf.bytesused == 0) {
                v4l2_capture_count_.fetch_add(1, std::memory_order_relaxed);
                rflow::core::rtc::PushPipelineDropStats::Instance().OnV4l2FrameCaptured(1);
                const uint64_t total = empty_capture_drop_count_.fetch_add(1, std::memory_order_relaxed) + 1;
                rflow::core::rtc::PushPipelineDropStats::Instance().LogDrop(
                    "Capture/EmptyPayload", 1, total, "reason=zero_bytesused");
                QBufV4l2Index(buf.index);
                continue;
            }
            v4l2_capture_count_.fetch_add(1, std::memory_order_relaxed);
            rflow::core::rtc::PushPipelineDropStats::Instance().OnV4l2FrameCaptured(1);
            if (mjpeg_decode_inline_) {
                static std::atomic<unsigned> inline_counter{0};
                const int64_t t0_us = webrtc::TimeMicros();
                ProcessV4l2CapturedFrame(buf.index, src, buf.bytesused, dq_time_us, v4l2_timestamp_us, poll_wait_us,
                                         dqbuf_ioctl_us, 0);
                if (LatencyTraceEnabled()) {
                    const unsigned n = ++inline_counter;
                    if ((n % 30u) == 0u) {
                        const double ms = static_cast<double>(webrtc::TimeMicros() - t0_us) / 1000.0;
                        RFLOW_LOG_TAG_I("Latency", "MJPEG inline decode+OnFrame ms=%f (sample#%u)", ms,
                                        static_cast<unsigned>(n));
                    }
                }
                QBufV4l2Index(buf.index);
                continue;
            }
            // 延迟 QBUF：解码线程从 mmap 读 JPEG 并入 MPP 后再归还驱动，去掉「整帧 memcpy 到队列」。
            {
                std::deque<MjpegPendingBuf> dropped;
                size_t stale_drop_n = 0;
                size_t queue_full_drop_n = 0;
                size_t latest_only_drop_n = 0;
                size_t qdepth_after = 0;
                {
                    std::unique_lock<std::mutex> lk(jpeg_queue_mu_);
                    const int64_t stale_budget_us = DecodeQueueStaleDropBudgetUs(requested_capture_fps_);
                    if (stale_budget_us > 0) {
                        const int64_t now_us = webrtc::TimeMicros();
                        while (!jpeg_queue_.empty()) {
                            const int64_t age_us = now_us - jpeg_queue_.front().enqueue_time_us;
                            if (age_us <= stale_budget_us) {
                                break;
                            }
                            dropped.push_back(jpeg_queue_.front());
                            jpeg_queue_.pop_front();
                            ++stale_drop_n;
                        }
                    }
                    if (mjpeg_queue_latest_only_) {
                        latest_only_drop_n = jpeg_queue_.size();
                        dropped.swap(jpeg_queue_);
                    } else {
                        while (jpeg_queue_.size() >= mjpeg_queue_max_) {
                            dropped.push_back(jpeg_queue_.front());
                            jpeg_queue_.pop_front();
                            ++queue_full_drop_n;
                        }
                    }
                    jpeg_queue_.push_back(
                        MjpegPendingBuf{buf.index, buf.bytesused, dq_time_us, v4l2_timestamp_us, poll_wait_us,
                                       dqbuf_ioctl_us, webrtc::TimeMicros()});
                    qdepth_after = jpeg_queue_.size();
                }
                if (stale_drop_n > 0) {
                    mjpeg_stale_drop_count_.fetch_add(stale_drop_n, std::memory_order_relaxed);
                }
                if (queue_full_drop_n > 0) {
                    mjpeg_queue_full_drop_count_.fetch_add(queue_full_drop_n, std::memory_order_relaxed);
                }
                if (latest_only_drop_n > 0) {
                    mjpeg_latest_only_drop_count_.fetch_add(latest_only_drop_n, std::memory_order_relaxed);
                }
                for (const MjpegPendingBuf& drop : dropped) {
                    QBufV4l2Index(drop.index);
                }
                if (stale_drop_n > 0 || queue_full_drop_n > 0 || latest_only_drop_n > 0) {
                    MaybeLogMjpegQueueDropStats(qdepth_after, stale_drop_n, queue_full_drop_n,
                                                latest_only_drop_n);
                }
            }
            jpeg_queue_cv_.notify_one();
            continue;
        }
        v4l2_capture_count_.fetch_add(1, std::memory_order_relaxed);
        rflow::core::rtc::PushPipelineDropStats::Instance().OnV4l2FrameCaptured(1);
        ProcessV4l2CapturedFrame(buf.index, src, buf.bytesused, dq_time_us, v4l2_timestamp_us, poll_wait_us,
                                 dqbuf_ioctl_us, 0);
        QBufV4l2Index(buf.index);
    }
}

#endif  // WEBRTC_LINUX && __linux__

void CameraVideoTrackSource::Stop() {
    negotiated_capture_fps_ = 0;
#if defined(WEBRTC_LINUX) && defined(__linux__)
    StopDirectV4l2();
#endif
    if (vcm_) {
        vcm_->DeRegisterCaptureDataCallback();
        vcm_->StopCapture();
        vcm_ = nullptr;
    }
    device_info_.reset();
}

bool CameraVideoTrackSource::Start(const char* device_unique_id, int width, int height, int fps,
                                   bool prefer_mpp_mjpeg_decode,
                                   const V4l2MjpegPipelineOptions* mjpeg_pipeline) {
    Stop();
    requested_capture_fps_ = fps > 0 ? fps : 30;
    prefer_mpp_mjpeg_decode_ = prefer_mpp_mjpeg_decode;
#if defined(WEBRTC_LINUX) && defined(__linux__)
    ApplyMjpegPipelineOptions(mjpeg_pipeline);
#endif
    if (!device_unique_id || !device_unique_id[0]) {
        return false;
    }
#if defined(WEBRTC_LINUX) && defined(__linux__)
    if (strncmp(device_unique_id, "/dev/video", 10) == 0) {
        if (StartDirectV4l2(device_unique_id, width, height, fps)) {
            return true;
        }
        RFLOW_LOG_TAG_E("CameraVideoTrackSource", "direct V4L2 open failed for %s",
                        device_unique_id);
        return false;
    }
#endif
    device_info_.reset(webrtc::VideoCaptureFactory::CreateDeviceInfo());
    if (!device_info_) {
        RTC_LOG(LS_ERROR) << "CreateDeviceInfo failed";
        return false;
    }
    vcm_ = webrtc::VideoCaptureFactory::Create(device_unique_id);
    if (!vcm_) {
        RTC_LOG(LS_ERROR) << "VideoCaptureFactory::Create failed for " << device_unique_id;
        return false;
    }
    vcm_->RegisterCaptureDataCallback(this);

    webrtc::VideoCaptureCapability requested;
    requested.width = width;
    requested.height = height;
    requested.maxFPS = fps;
    requested.videoType = webrtc::VideoType::kI420;

    webrtc::VideoCaptureCapability used;
    if (device_info_->GetBestMatchedCapability(device_unique_id, requested, used) < 0) {
        if (device_info_->NumberOfCapabilities(device_unique_id) > 0 &&
            device_info_->GetCapability(device_unique_id, 0, used) == 0) {
        } else {
            used = requested;
        }
    }

    if (vcm_->StartCapture(used) != 0) {
        RTC_LOG(LS_ERROR) << "StartCapture failed";
        vcm_->DeRegisterCaptureDataCallback();
        vcm_ = nullptr;
        return false;
    }
    negotiated_capture_fps_ = used.maxFPS > 0 ? used.maxFPS : (fps > 0 ? fps : 30);
    return true;
}

void CameraVideoTrackSource::OnFrame(const webrtc::VideoFrame& frame) {
#if defined(WEBRTC_LINUX) && defined(__linux__) && defined(RFLOW_HAVE_ROCKCHIP_MPP)
    if (rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer* native =
            rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer::TryGet(frame.video_frame_buffer())) {
        native->SetOnFrameEnterUs(webrtc::TimeMicros());
    }
#endif
    captured_frames_.fetch_add(1, std::memory_order_relaxed);
#if defined(WEBRTC_LINUX) && defined(__linux__)
    if (direct_fd_ < 0) {
        v4l2_capture_count_.fetch_add(1, std::memory_order_relaxed);
        rflow::core::rtc::PushPipelineDropStats::Instance().OnV4l2FrameCaptured(1);
    }
#endif
    rflow::core::rtc::PushPipelineDropStats::Instance().OnFrameDispatched(1);
    AdaptedVideoTrackSource::OnFrame(frame);
}

void CameraVideoTrackSource::MaybeLogMjpegQueueDropStats(size_t queue_depth,
                                                         uint64_t stale_delta,
                                                         uint64_t queue_full_delta,
                                                         uint64_t latest_only_delta) {
    const uint64_t drop_delta = stale_delta + queue_full_delta + latest_only_delta;
    if (drop_delta == 0) {
        return;
    }
    const uint64_t stale = mjpeg_stale_drop_count_.load(std::memory_order_relaxed);
    const uint64_t queue_full = mjpeg_queue_full_drop_count_.load(std::memory_order_relaxed);
    const uint64_t latest_only = mjpeg_latest_only_drop_count_.load(std::memory_order_relaxed);
    const uint64_t total_drops = stale + queue_full + latest_only;
    char detail[192];
    snprintf(detail, sizeof(detail),
             "qdepth=%zu | totals[stale=%llu queue_full=%llu latest_only=%llu] | "
             "window[stale=%llu queue_full=%llu latest_only=%llu]",
             queue_depth, static_cast<unsigned long long>(stale),
             static_cast<unsigned long long>(queue_full), static_cast<unsigned long long>(latest_only),
             static_cast<unsigned long long>(stale_delta), static_cast<unsigned long long>(queue_full_delta),
             static_cast<unsigned long long>(latest_only_delta));
    rflow::core::rtc::PushPipelineDropStats::Instance().LogDrop("Capture/MJPEG_QUEUE", drop_delta,
                                                                     total_drops, detail);
}

#if defined(WEBRTC_LINUX) && defined(__linux__) && defined(RFLOW_HAVE_ROCKCHIP_MPP)
bool CameraVideoTrackSource::WantV4l2ExtDmabufToMpp() const {
    return rflow::service::impl::policy::EvaluateMjpegZeroCopyPolicy(
               v4l2_ext_dma_config_, mjpeg_rga_config_)
        .use_v4l2_ext_dmabuf;
}

bool CameraVideoTrackSource::WantMjpegRgaToMpp() const {
    return rflow::service::impl::policy::EvaluateMjpegZeroCopyPolicy(
               v4l2_ext_dma_config_, mjpeg_rga_config_)
        .use_rga_to_mpp;
}
#endif

CameraVideoTrackSource::PipelineHealthSnapshot CameraVideoTrackSource::GetPipelineHealthSnapshot() const {
    PipelineHealthSnapshot snap;
    snap.v4l2_capture = v4l2_capture_count_.load(std::memory_order_relaxed);
    snap.dispatched = captured_frames_.load(std::memory_order_relaxed);
    snap.mjpeg_stale_drops = mjpeg_stale_drop_count_.load(std::memory_order_relaxed);
    snap.mjpeg_queue_full_drops = mjpeg_queue_full_drop_count_.load(std::memory_order_relaxed);
    snap.mjpeg_latest_only_drops = mjpeg_latest_only_drop_count_.load(std::memory_order_relaxed);
    snap.empty_payload_drops = empty_capture_drop_count_.load(std::memory_order_relaxed);
    snap.convert_fail_drops = convert_fail_drop_count_.load(std::memory_order_relaxed);
    snap.nv12_pool_busy = nv12_pool_busy_count_.load(std::memory_order_relaxed);
#if defined(WEBRTC_LINUX) && defined(__linux__)
    std::lock_guard<std::mutex> lk(jpeg_queue_mu_);
    snap.mjpeg_queue_depth = jpeg_queue_.size();
#endif
    return snap;
}

}  // namespace rflow::service::impl

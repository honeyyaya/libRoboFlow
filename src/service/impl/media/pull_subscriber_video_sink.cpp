#include "media/pull_subscriber_video_sink.h"

#include <atomic>
#include <iostream>

#include "api/video/video_frame_buffer.h"
#include "core/runtime/runtime_knobs.h"
#include "libyuv/convert.h"
#include "media/pull_subscriber_internals.h"
#include "rtc_base/time_utils.h"

namespace rflow::service::impl::observers {

namespace {

using detail::pull::MediaTimingTraceEnabled;
using detail::pull::MediaTimingTraceEveryN;

}  // namespace

VideoSink::VideoSink(Callback cb, bool skip_argb_conversion, StatsCallback stats_cb)
    : on_frame_(std::move(cb)),
      skip_argb_conversion_(skip_argb_conversion),
      on_frame_stats_(std::move(stats_cb)) {}

void VideoSink::OnFrame(const webrtc::VideoFrame& frame) {
    const bool e2e_trace = rflow::core::runtime::ReadBool("WEBRTC_E2E_LATENCY_TRACE");
    const int64_t t_sink_enter_us = e2e_trace ? webrtc::TimeMicros() : 0;
    const int64_t t_now_us = webrtc::TimeMicros();
    const int64_t wall_sink_utc_ms = e2e_trace ? webrtc::TimeUTCMillis() : int64_t{0};
    if (!on_frame_) {
        return;
    }
    auto buf = frame.video_frame_buffer();
    if (!buf) {
        return;
    }
    int w = frame.width();
    int h = frame.height();
    if (w <= 0 || h <= 0) {
        return;
    }
    if (skip_argb_conversion_) {
        const int64_t t_fast_done_us = e2e_trace ? webrtc::TimeMicros() : 0;
        static unsigned frame_count = 0;
        unsigned n = ++frame_count;
        if (n == 1 || n <= 5 || n % 30 == 0) {
            std::cout << "[VideoSink] OnFrame #" << n << " (skip ARGB)" << std::endl;
        }
        const int64_t t_callback_done_us = e2e_trace ? webrtc::TimeMicros() : 0;
        on_frame_(nullptr, w, h, 0, frame.id(), t_callback_done_us);
        if (on_frame_stats_) {
            on_frame_stats_(frame.id(), t_callback_done_us);
        }
        if (MediaTimingTraceEnabled()) {
            static std::atomic<unsigned> media_sink_n{0};
            const unsigned n_trace = ++media_sink_n;
            if ((n_trace % MediaTimingTraceEveryN()) == 0u) {
                std::cout << "[MEDIA_TIMING][sink] t_us=" << t_now_us << " trace_id="
                          << static_cast<unsigned>(frame.id()) << " rtp_ts=" << frame.rtp_timestamp()
                          << " event=onframe_skip_argb t_callback_done_us=" << t_callback_done_us
                          << std::endl;
            }
        }
        if (e2e_trace) {
            std::cout << "[E2E_RX] rtp_ts=" << frame.rtp_timestamp() << " trace_id="
                      << static_cast<unsigned>(frame.id()) << " frame_id="
                      << static_cast<unsigned>(frame.id()) << " t_sink_us=" << t_sink_enter_us
                      << " wall_utc_ms=" << wall_sink_utc_ms << " t_argb_done_us=" << t_fast_done_us
                      << " t_callback_done_us=" << t_callback_done_us << std::endl;
        }
        return;
    }
    int stride = w * 4;
    auto i420 = buf->ToI420();
    if (!i420) {
        return;
    }
    argb_.resize(static_cast<size_t>(stride * h));
    libyuv::I420ToARGB(i420->DataY(), i420->StrideY(), i420->DataU(), i420->StrideU(), i420->DataV(),
                       i420->StrideV(), argb_.data(), stride, w, h);
    const int64_t t_argb_done_us = e2e_trace ? webrtc::TimeMicros() : 0;
    static unsigned frame_count = 0;
    unsigned n = ++frame_count;
    if (n == 1 || n <= 5 || n % 30 == 0) {
        std::cout << "[VideoSink] OnFrame #" << n << std::endl;
    }
    const int64_t t_callback_done_us = e2e_trace ? webrtc::TimeMicros() : 0;
    on_frame_(argb_.data(), w, h, stride, frame.id(), t_callback_done_us);
    MaybeShrinkArgbBuffer(w, h, n);
    if (on_frame_stats_) {
        on_frame_stats_(frame.id(), t_callback_done_us);
    }
    if (MediaTimingTraceEnabled()) {
        static std::atomic<unsigned> media_sink_n{0};
        const unsigned n_trace = ++media_sink_n;
        if ((n_trace % MediaTimingTraceEveryN()) == 0u) {
            std::cout << "[MEDIA_TIMING][sink] t_us=" << t_now_us << " trace_id="
                      << static_cast<unsigned>(frame.id()) << " rtp_ts=" << frame.rtp_timestamp()
                      << " event=onframe_argb_done t_argb_done_us=" << t_argb_done_us
                      << " t_callback_done_us=" << t_callback_done_us << std::endl;
        }
    }
    if (e2e_trace) {
        std::cout << "[E2E_RX] rtp_ts=" << frame.rtp_timestamp() << " trace_id="
                  << static_cast<unsigned>(frame.id()) << " frame_id="
                  << static_cast<unsigned>(frame.id()) << " t_sink_us=" << t_sink_enter_us
                  << " wall_utc_ms=" << wall_sink_utc_ms << " t_argb_done_us=" << t_argb_done_us
                  << " t_callback_done_us=" << t_callback_done_us << std::endl;
    }
}

void VideoSink::MaybeShrinkArgbBuffer(int w, int h, unsigned frame_index) {
    if (frame_index == 0 || (frame_index % 300u) != 0u || w <= 0 || h <= 0) {
        return;
    }
    const size_t expected = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    if (expected == 0 || argb_.capacity() <= expected * 4u || argb_.size() != expected) {
        return;
    }
    std::vector<uint8_t> compact;
    compact.assign(argb_.begin(), argb_.end());
    argb_.swap(compact);
}

}  // namespace rflow::service::impl::observers

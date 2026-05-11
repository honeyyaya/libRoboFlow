/**
 * @file   stream.cpp
 * @brief  open_stream / close_stream / get_stats
 */

#include "rflow/Client/librflow_client_api.h"

#include "internal/handles.h"
#include "internal/state_ops.h"
#include "internal/state.h"

#include "abi/handle.h"
#include "base/stream_handle_ops.h"
#include "media/frame_types.h"
#include "media/stream_stats.h"
#include "public/last_error_api.h"
#include "public/logger_api.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <utility>

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
#  include "impl/rtc_stream/rtc_stream_frame_converter.h"
#  include "api/video/video_frame.h"
#endif

namespace {

constexpr const char* kErrorOrigin = "client/stream";

/// 仅填充与 RTC 快照无关的时长/收帧计数；QoS 字段由调用方决定是否再调 `CollectRtcStreamStats`。
void FillClientStreamBaseStats(const librflow_stream_s& stream, librflow_stream_stats_s* ms) {
    rflow::common::media::FillStreamStatsBase(
        *ms,
        stream.opened_at,
        stream.video_frames_received.load(std::memory_order_relaxed));
}

void ResetOpenedStats(librflow_stream_s& sh) {
    const auto now = std::chrono::steady_clock::now();
    sh.opened_at = now;
    sh.last_stats_emit_at = now;
    sh.video_frames_received.store(0, std::memory_order_relaxed);
    sh.last_stats_frames_received.store(0, std::memory_order_relaxed);
}

void MaybeEmitPeriodicStats(librflow_stream_s& sh) {
    if (!sh.cb.on_stream_stats) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (sh.last_stats_emit_at != std::chrono::steady_clock::time_point{} &&
        now - sh.last_stats_emit_at < std::chrono::seconds(1)) {
        return;
    }

    auto stats = rflow::common::media::AllocStreamStats();
    if (!stats) {
        return;
    }

    auto* ms = const_cast<librflow_stream_stats_s*>(stats);
    FillClientStreamBaseStats(sh, ms);
#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
    if (sh.impl) {
        rflow::client::internal::CollectRtcStreamStats(sh.impl, ms);
    }
#endif

    const uint64_t current = sh.video_frames_received.load(std::memory_order_relaxed);
    const uint64_t previous =
        sh.last_stats_frames_received.exchange(current, std::memory_order_relaxed);
    const int64_t elapsed_ms = sh.last_stats_emit_at == std::chrono::steady_clock::time_point{}
        ? 1000
        : std::max<int64_t>(
              1,
              std::chrono::duration_cast<std::chrono::milliseconds>(now - sh.last_stats_emit_at)
                  .count());
    ms->fps = static_cast<uint32_t>(((current - previous) * 1000ULL) / elapsed_ms);
    sh.last_stats_emit_at = now;

    sh.cb.on_stream_stats(stats, sh.cb.userdata);
    librflow_stream_stats_release(stats);
}

}  // namespace

extern "C" {

rflow_err_t librflow_open_stream(int32_t index,
                                 librflow_stream_param_t param,
                                 librflow_stream_cb_t cb,
                                 librflow_stream_handle_t* out_handle) {
    if (!out_handle) return RFLOW_ERR_PARAM;
    *out_handle = nullptr;

    if (param && param->magic != rflow::client::kMagicStreamParam) return RFLOW_ERR_PARAM;
    if (!cb || cb->magic != rflow::client::kMagicStreamCb) return RFLOW_ERR_PARAM;

    auto& s = rflow::client::state();

    auto sh = std::make_shared<librflow_stream_s>();
    sh->magic = rflow::client::kMagicStream;
    sh->index = index;
    sh->state.store(RFLOW_STREAM_IDLE);
    sh->cb = *cb;

    {
        std::lock_guard<std::mutex> lk(s.mu);
        if (s.lifecycle != rflow::client::LifecycleState::kConnected) {
            rflow::set_last_error(RFLOW_ERR_STATE,
                                  "must connect before open_stream",
                                  kErrorOrigin);
            return RFLOW_ERR_STATE;
        }
        for (auto& [h, x] : s.streams) {
            if (x && x->index == index) {
                rflow::set_last_error(RFLOW_ERR_STREAM_ALREADY_OPEN,
                                      "stream index already open",
                                      kErrorOrigin);
                return RFLOW_ERR_STREAM_ALREADY_OPEN;
            }
        }
        s.streams.emplace(sh.get(), sh);
    }

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
    std::weak_ptr<librflow_stream_s> wsh = sh;
    const int32_t captured_index = index;
    auto seq_counter = std::make_shared<std::atomic<uint32_t>>(0);

    auto state_sink = [wsh](rflow_stream_state_t st, rflow_err_t reason) {
        auto p = wsh.lock();
        if (!p) return;
        p->state.store(st);
        if (st == RFLOW_STREAM_OPENED) {
            ResetOpenedStats(*p);
        }
        if (p->cb.on_state) {
            p->cb.on_state(p.get(), st, reason, p->cb.userdata);
        }
    };

    auto frame_sink = [wsh, captured_index, seq_counter](const webrtc::VideoFrame& vf) {
        auto p = wsh.lock();
        if (!p) return;

        const uint32_t seq = seq_counter->fetch_add(1, std::memory_order_relaxed);
        auto f = rflow::client::impl::MakeVideoFrameFromRtcFrame(vf, captured_index, seq);
        if (!f) return;

        p->video_frames_received.fetch_add(1, std::memory_order_relaxed);

        if (p->cb.on_video) {
            p->cb.on_video(p.get(), f, p->cb.userdata);
        }
        librflow_video_frame_release(f);

        MaybeEmitPeriodicStats(*p);
    };

    std::shared_ptr<void> impl;
    rflow_err_t e = rflow::client::internal::OpenRtcStreamSession(
        index, param, std::move(state_sink), std::move(frame_sink), &impl);
    if (e != RFLOW_OK) {
        std::lock_guard<std::mutex> lk(s.mu);
        s.streams.erase(sh.get());
        sh->magic = 0;
        return e;
    }

    sh->impl = std::move(impl);
#else
    sh->state.store(RFLOW_STREAM_OPENED);
    ResetOpenedStats(*sh);
    if (sh->cb.on_state) {
        sh->cb.on_state(sh.get(), RFLOW_STREAM_OPENED, RFLOW_OK, sh->cb.userdata);
    }
#endif

    *out_handle = sh.get();
    RFLOW_LOGI("librflow_open_stream index=%d OK", index);
    return RFLOW_OK;
}

rflow_err_t librflow_close_stream(librflow_stream_handle_t handle) {
    if (!handle) return RFLOW_OK;

    auto& s = rflow::client::state();
    auto sh = rflow::common::base::RemoveStreamUnlocked(s, handle);
    if (!sh) return RFLOW_OK;

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
    if (auto impl = std::move(sh->impl)) {
        rflow::client::internal::CloseRtcStreamSession(std::move(impl));
    } else
#endif
    {
        rflow::client::internal::MarkStreamClosed(sh, handle);
    }
    if (sh->magic != 0) {
        sh->magic = 0;
    }
    RFLOW_LOGI("librflow_close_stream idx=%d OK", sh->index);
    return RFLOW_OK;
}

rflow_err_t librflow_stream_get_stats(librflow_stream_handle_t handle,
                                      librflow_stream_stats_t* out_stats) {
    if (!handle || handle->magic != rflow::client::kMagicStream) return RFLOW_ERR_PARAM;
    if (!out_stats) return RFLOW_ERR_PARAM;

    auto stats = rflow::common::media::AllocStreamStats();
    if (!stats) {
        *out_stats = nullptr;
        return RFLOW_ERR_FAIL;
    }

    auto* ms = const_cast<librflow_stream_stats_s*>(stats);
    FillClientStreamBaseStats(*handle, ms);

    bool collected = false;
#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
    if (handle->impl) {
        collected = rflow::client::internal::CollectRtcStreamStats(handle->impl, ms);
    }
#endif
    if (!collected) {
        rflow::common::media::ApplyStreamStatsFpsFallbackFromFrameCount(
            *ms, handle->video_frames_received.load(std::memory_order_relaxed));
    }

    *out_stats = stats;
    return RFLOW_OK;
}

}  // extern "C"

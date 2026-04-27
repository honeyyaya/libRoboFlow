/**
 * @file   stream.cpp
 * @brief  open_stream / close_stream / get_stats
 */

#include "rflow/Client/librflow_client_api.h"

#include "internal/handles.h"
#include "internal/state.h"

#include "common/internal/handle.h"
#include "common/internal/last_error.h"
#include "common/internal/logger.h"
#include "common/internal/frame_impl.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <new>
#include <utility>

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
#  include "impl/rtc_stream/rtc_stream_frame_converter.h"
#  include "impl/rtc_stream/rtc_stream_manager.h"
#  include "impl/rtc_stream/rtc_stream_session.h"
#  include "api/video/video_frame.h"
#endif

namespace {

librflow_stream_stats_t MakeEmptyStatsSnapshot() {
    auto* s = new (std::nothrow) librflow_stream_stats_s();
    if (!s) return nullptr;
    s->magic = rflow::kMagicStreamStats;
    s->refcount.store(1, std::memory_order_relaxed);
    return s;
}

void FillLocalStatsSnapshot(const librflow_stream_s& sh, librflow_stream_stats_s* stats) {
    if (!stats) return;
    const auto now = std::chrono::steady_clock::now();
    if (sh.opened_at != std::chrono::steady_clock::time_point{}) {
        stats->duration_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - sh.opened_at).count());
    }
    stats->in_bound_pkts = sh.video_frames_received.load(std::memory_order_relaxed);
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

    auto stats = MakeEmptyStatsSnapshot();
    if (!stats) {
        return;
    }

    auto* ms = const_cast<librflow_stream_stats_s*>(stats);
    FillLocalStatsSnapshot(sh, ms);

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
    if (sh.impl) {
        auto pull = std::static_pointer_cast<rflow::client::impl::RtcStreamSession>(sh.impl);
        pull->CollectStats(ms);
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
            rflow::set_last_error("must connect before open_stream");
            return RFLOW_ERR_STATE;
        }
        for (auto& [h, x] : s.streams) {
            if (x && x->index == index) {
                rflow::set_last_error("stream index already open");
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

    std::shared_ptr<rflow::client::impl::RtcStreamSession> pull;
    rflow_err_t e = rflow::client::impl::RtcStreamManager::Instance().OpenStream(
        index, param, std::move(state_sink), std::move(frame_sink), &pull);
    if (e != RFLOW_OK) {
        std::lock_guard<std::mutex> lk(s.mu);
        s.streams.erase(sh.get());
        sh->magic = 0;
        return e;
    }

    sh->impl = std::static_pointer_cast<void>(pull);
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

    std::shared_ptr<librflow_stream_s> sh;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        auto it = s.streams.find(handle);
        if (it == s.streams.end()) return RFLOW_OK;
        sh = std::move(it->second);
        s.streams.erase(it);
    }

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
    if (auto impl = std::move(sh->impl)) {
        auto pull = std::static_pointer_cast<rflow::client::impl::RtcStreamSession>(impl);
        pull->Close();
    } else
#endif
    {
        sh->state.store(RFLOW_STREAM_CLOSED);
        if (sh->cb.on_state) {
            sh->cb.on_state(handle, RFLOW_STREAM_CLOSED, RFLOW_OK, sh->cb.userdata);
        }
    }

    sh->magic = 0;
    RFLOW_LOGI("librflow_close_stream idx=%d OK", sh->index);
    return RFLOW_OK;
}

rflow_err_t librflow_stream_get_stats(librflow_stream_handle_t handle,
                                      librflow_stream_stats_t* out_stats) {
    if (!handle || handle->magic != rflow::client::kMagicStream) return RFLOW_ERR_PARAM;
    if (!out_stats) return RFLOW_ERR_PARAM;

    auto stats = MakeEmptyStatsSnapshot();
    if (!stats) {
        *out_stats = nullptr;
        return RFLOW_ERR_FAIL;
    }

    auto* ms = const_cast<librflow_stream_stats_s*>(stats);
    FillLocalStatsSnapshot(*handle, ms);

#if defined(RFLOW_RTC_WEBRTC_PEER_CONNECTION_API)
    bool collected = false;
    if (handle->impl) {
        auto pull = std::static_pointer_cast<rflow::client::impl::RtcStreamSession>(handle->impl);
        collected = pull->CollectStats(ms);
    }
    if (!collected && ms->fps == 0) {
        const uint32_t duration_ms = std::max<uint32_t>(1, ms->duration_ms);
        const uint64_t frames = handle->video_frames_received.load(std::memory_order_relaxed);
        ms->fps = static_cast<uint32_t>((frames * 1000ULL) / duration_ms);
    }
#else
    const uint32_t duration_ms = std::max<uint32_t>(1, ms->duration_ms);
    const uint64_t frames = handle->video_frames_received.load(std::memory_order_relaxed);
    ms->fps = static_cast<uint32_t>((frames * 1000ULL) / duration_ms);
#endif

    *out_stats = stats;
    return RFLOW_OK;
}

}  // extern "C"

#include "rflow/librflow_common.h"

#include "media/frame_types.h"
#include "media/stream_stats.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <new>

namespace rflow::common::media {

librflow_stream_stats_t AllocStreamStats() {
    auto* s = new (std::nothrow) librflow_stream_stats_s();
    if (!s) return nullptr;
    s->magic = rflow::kMagicStreamStats;
    s->refcount.store(1, std::memory_order_relaxed);
    return s;
}

void FillStreamStatsBase(librflow_stream_stats_s& stats,
                         std::chrono::steady_clock::time_point opened_at,
                         uint64_t in_bound_pkts) {
    const auto now = std::chrono::steady_clock::now();
    if (opened_at != std::chrono::steady_clock::time_point{}) {
        stats.duration_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - opened_at).count());
    }
    stats.in_bound_pkts = in_bound_pkts;
}

void ApplyStreamStatsFpsFallbackFromFrameCount(librflow_stream_stats_s& stats,
                                               uint64_t frame_count) {
    if (stats.fps != 0) {
        return;
    }
    const uint32_t duration_ms = std::max<uint32_t>(1, stats.duration_ms);
    stats.fps = static_cast<uint32_t>((frame_count * 1000ULL) / duration_ms);
}

}  // namespace rflow::common::media

extern "C" {

librflow_stream_stats_t librflow_stream_stats_retain(librflow_stream_stats_t stats) {
    if (!stats || stats->magic != rflow::kMagicStreamStats) return nullptr;
    const_cast<librflow_stream_stats_s*>(stats)->refcount.fetch_add(1, std::memory_order_relaxed);
    return stats;
}

void librflow_stream_stats_release(librflow_stream_stats_t stats) {
    if (!stats || stats->magic != rflow::kMagicStreamStats) return;
    auto* mutable_stats = const_cast<librflow_stream_stats_s*>(stats);
    if (mutable_stats->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        mutable_stats->magic = 0;
        delete mutable_stats;
    }
}

uint32_t librflow_stream_stats_get_duration_ms(librflow_stream_stats_t stats) { return stats ? stats->duration_ms : 0; }
uint64_t librflow_stream_stats_get_in_bound_bytes(librflow_stream_stats_t stats) { return stats ? stats->in_bound_bytes : 0; }
uint64_t librflow_stream_stats_get_in_bound_pkts(librflow_stream_stats_t stats) { return stats ? stats->in_bound_pkts : 0; }
uint64_t librflow_stream_stats_get_out_bound_bytes(librflow_stream_stats_t stats) { return stats ? stats->out_bound_bytes : 0; }
uint64_t librflow_stream_stats_get_out_bound_pkts(librflow_stream_stats_t stats) { return stats ? stats->out_bound_pkts : 0; }
uint32_t librflow_stream_stats_get_lost_pkts(librflow_stream_stats_t stats) { return stats ? stats->lost_pkts : 0; }
uint32_t librflow_stream_stats_get_bitrate_kbps(librflow_stream_stats_t stats) { return stats ? stats->bitrate_kbps : 0; }
uint32_t librflow_stream_stats_get_rtt_ms(librflow_stream_stats_t stats) { return stats ? stats->rtt_ms : 0; }
uint32_t librflow_stream_stats_get_fps(librflow_stream_stats_t stats) { return stats ? stats->fps : 0; }
uint32_t librflow_stream_stats_get_jitter_ms(librflow_stream_stats_t stats) { return stats ? stats->jitter_ms : 0; }
uint32_t librflow_stream_stats_get_freeze_count(librflow_stream_stats_t stats) { return stats ? stats->freeze_count : 0; }
uint32_t librflow_stream_stats_get_decode_fail_count(librflow_stream_stats_t stats) {
    return stats ? stats->decode_fail_count : 0;
}

uint32_t librflow_stream_stats_get_jitter_buffer_delay_ms(librflow_stream_stats_t stats) {
    return stats ? stats->jitter_buffer_delay_ms : 0;
}

uint32_t librflow_stream_stats_get_jitter_min_delay_ms(librflow_stream_stats_t stats) {
    return stats ? stats->jitter_min_delay_ms : 0;
}

}  // extern "C"

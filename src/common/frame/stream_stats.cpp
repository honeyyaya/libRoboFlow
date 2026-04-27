#include "rflow/librflow_common.h"

#include "../internal/frame_impl.h"
#include "../internal/handle.h"

#include <atomic>

extern "C" {

librflow_stream_stats_t librflow_stream_stats_retain(librflow_stream_stats_t s) {
    if (!s || s->magic != rflow::kMagicStreamStats) return nullptr;
    const_cast<librflow_stream_stats_s*>(s)->refcount.fetch_add(1, std::memory_order_relaxed);
    return s;
}

void librflow_stream_stats_release(librflow_stream_stats_t s) {
    if (!s || s->magic != rflow::kMagicStreamStats) return;
    auto* ms = const_cast<librflow_stream_stats_s*>(s);
    if (ms->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        ms->magic = 0;
        delete ms;
    }
}

uint32_t librflow_stream_stats_get_duration_ms    (librflow_stream_stats_t s) { return s ? s->duration_ms     : 0; }
uint64_t librflow_stream_stats_get_in_bound_bytes (librflow_stream_stats_t s) { return s ? s->in_bound_bytes  : 0; }
uint64_t librflow_stream_stats_get_in_bound_pkts  (librflow_stream_stats_t s) { return s ? s->in_bound_pkts   : 0; }
uint64_t librflow_stream_stats_get_out_bound_bytes(librflow_stream_stats_t s) { return s ? s->out_bound_bytes : 0; }
uint64_t librflow_stream_stats_get_out_bound_pkts (librflow_stream_stats_t s) { return s ? s->out_bound_pkts  : 0; }
uint32_t librflow_stream_stats_get_lost_pkts      (librflow_stream_stats_t s) { return s ? s->lost_pkts       : 0; }
uint32_t librflow_stream_stats_get_bitrate_kbps   (librflow_stream_stats_t s) { return s ? s->bitrate_kbps    : 0; }
uint32_t librflow_stream_stats_get_rtt_ms         (librflow_stream_stats_t s) { return s ? s->rtt_ms          : 0; }
uint32_t librflow_stream_stats_get_fps            (librflow_stream_stats_t s) { return s ? s->fps             : 0; }
uint32_t librflow_stream_stats_get_jitter_ms      (librflow_stream_stats_t s) { return s ? s->jitter_ms       : 0; }
uint32_t librflow_stream_stats_get_freeze_count   (librflow_stream_stats_t s) { return s ? s->freeze_count    : 0; }
uint32_t librflow_stream_stats_get_decode_fail_count(librflow_stream_stats_t s) {
    return s ? s->decode_fail_count : 0;
}

}  // extern "C"

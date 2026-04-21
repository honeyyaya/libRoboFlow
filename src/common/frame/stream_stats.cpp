#include "robrt/librobrt_common.h"

#include "../internal/frame_impl.h"

extern "C" {

uint32_t librobrt_stream_stats_get_duration_ms    (librobrt_stream_stats_t s) { return s ? s->duration_ms     : 0; }
uint64_t librobrt_stream_stats_get_in_bound_bytes (librobrt_stream_stats_t s) { return s ? s->in_bound_bytes  : 0; }
uint64_t librobrt_stream_stats_get_in_bound_pkts  (librobrt_stream_stats_t s) { return s ? s->in_bound_pkts   : 0; }
uint64_t librobrt_stream_stats_get_out_bound_bytes(librobrt_stream_stats_t s) { return s ? s->out_bound_bytes : 0; }
uint64_t librobrt_stream_stats_get_out_bound_pkts (librobrt_stream_stats_t s) { return s ? s->out_bound_pkts  : 0; }
uint32_t librobrt_stream_stats_get_lost_pkts      (librobrt_stream_stats_t s) { return s ? s->lost_pkts       : 0; }
uint32_t librobrt_stream_stats_get_bitrate_kbps   (librobrt_stream_stats_t s) { return s ? s->bitrate_kbps    : 0; }
uint32_t librobrt_stream_stats_get_rtt_ms         (librobrt_stream_stats_t s) { return s ? s->rtt_ms          : 0; }
uint32_t librobrt_stream_stats_get_fps            (librobrt_stream_stats_t s) { return s ? s->fps             : 0; }

}  // extern "C"

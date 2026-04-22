#include "rflow/librflow_common.h"

#include "../internal/frame_impl.h"

extern "C" {

uint32_t librflow_stream_stats_get_duration_ms    (librflow_stream_stats_t s) { return s ? s->duration_ms     : 0; }
uint64_t librflow_stream_stats_get_in_bound_bytes (librflow_stream_stats_t s) { return s ? s->in_bound_bytes  : 0; }
uint64_t librflow_stream_stats_get_in_bound_pkts  (librflow_stream_stats_t s) { return s ? s->in_bound_pkts   : 0; }
uint64_t librflow_stream_stats_get_out_bound_bytes(librflow_stream_stats_t s) { return s ? s->out_bound_bytes : 0; }
uint64_t librflow_stream_stats_get_out_bound_pkts (librflow_stream_stats_t s) { return s ? s->out_bound_pkts  : 0; }
uint32_t librflow_stream_stats_get_lost_pkts      (librflow_stream_stats_t s) { return s ? s->lost_pkts       : 0; }
uint32_t librflow_stream_stats_get_bitrate_kbps   (librflow_stream_stats_t s) { return s ? s->bitrate_kbps    : 0; }
uint32_t librflow_stream_stats_get_rtt_ms         (librflow_stream_stats_t s) { return s ? s->rtt_ms          : 0; }
uint32_t librflow_stream_stats_get_fps            (librflow_stream_stats_t s) { return s ? s->fps             : 0; }

}  // extern "C"

#ifndef __RFLOW_COMMON_MEDIA_STREAM_STATS_H__
#define __RFLOW_COMMON_MEDIA_STREAM_STATS_H__

#include "media/frame_types.h"
#include "rflow/librflow_common.h"

#include <chrono>
#include <cstdint>

// 通用 stream stats 工具：
//   - AllocStreamStats     : 申请带 magic + refcount=1 的快照，供回调外发或 SDK 内部使用
//   - FillStreamStatsBase  : 填入与具体 RTC 后端无关的字段（duration_ms / in_bound_pkts）
// RTC 后端相关收集（CollectStats 等）由调用方各自处理；本层只负责 stats 对象的"骨架"。
namespace rflow::common::media {

librflow_stream_stats_t AllocStreamStats();

void FillStreamStatsBase(librflow_stream_stats_s& stats,
                         std::chrono::steady_clock::time_point opened_at,
                         uint64_t in_bound_pkts);

/// 若 `stats.fps` 仍为 0，则按已填的 `duration_ms` 与 `frame_count` 估算 FPS（Client/Service get_stats 兜底共用）。
void ApplyStreamStatsFpsFallbackFromFrameCount(librflow_stream_stats_s& stats,
                                               uint64_t frame_count);

}  // namespace rflow::common::media

#endif  // __RFLOW_COMMON_MEDIA_STREAM_STATS_H__

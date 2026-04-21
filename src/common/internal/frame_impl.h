/**
 * @file   frame_impl.h
 * @brief  video_frame / audio_frame / stream_stats 的内部表示
 *
 * 这些对象采用引用计数：SDK 内部线程构造 frame，回调交给业务；业务层
 * 若调用 retain，计数 +1；release 时 -1，归 0 时回收底层内存。
**/

#ifndef __ROBRT_INTERNAL_FRAME_IMPL_H__
#define __ROBRT_INTERNAL_FRAME_IMPL_H__

#include <atomic>
#include <cstdint>
#include <vector>

#include "handle.h"

struct librobrt_video_frame_s {
    uint32_t                magic;
    std::atomic<int32_t>    refcount;

    robrt_codec_t           codec;
    robrt_frame_type_t      type;
    uint32_t                width;
    uint32_t                height;
    uint64_t                pts_ms;
    uint64_t                utc_ms;
    uint32_t                seq;
    int32_t                 stream_index;

    std::vector<uint8_t>    payload;
};

struct librobrt_audio_frame_s {
    uint32_t                magic;
    std::atomic<int32_t>    refcount;

    robrt_audio_codec_t     codec;
    uint32_t                sample_rate;
    uint32_t                channel;
    uint32_t                sample_bit;
    uint64_t                pts_ms;
    uint64_t                utc_ms;
    uint32_t                seq;
    int32_t                 stream_index;

    std::vector<uint8_t>    payload;
};

struct librobrt_stream_stats_s {
    uint32_t magic;

    uint32_t duration_ms;
    uint64_t in_bound_bytes;
    uint64_t in_bound_pkts;
    uint64_t out_bound_bytes;
    uint64_t out_bound_pkts;
    uint32_t lost_pkts;
    uint32_t bitrate_kbps;
    uint32_t rtt_ms;
    uint32_t fps;
};

#endif  // __ROBRT_INTERNAL_FRAME_IMPL_H__

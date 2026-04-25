/**
 * @file   frame_impl.h
 * @brief  video_frame / stream_stats 的内部表示
 *
 * 这些对象采用引用计数：SDK 内部线程构造 frame，回调交给业务；业务层
 * 若调用 retain，计数 +1；release 时 -1，归 0 时回收底层内存。
**/

#ifndef __RFLOW_INTERNAL_FRAME_IMPL_H__
#define __RFLOW_INTERNAL_FRAME_IMPL_H__

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "handle.h"
#include "api/scoped_refptr.h"
#include "api/video/video_frame_buffer.h"

struct librflow_video_frame_s {
    uint32_t                magic;
    std::atomic<int32_t>    refcount;

    rflow_video_frame_backend_t backend = RFLOW_VIDEO_FRAME_BACKEND_UNKNOWN;
    rflow_native_handle_type_t  native_handle_type = RFLOW_NATIVE_HANDLE_NONE;
    rflow_codec_t           codec;
    rflow_frame_type_t      type;
    uint32_t                width;
    uint32_t                height;
    uint64_t                pts_ms;
    uint64_t                utc_ms;
    uint32_t                seq;
    int32_t                 stream_index;

    uint32_t                plane_count = 0;
    const uint8_t*          plane_data[3] = {nullptr, nullptr, nullptr};
    uint32_t                plane_strides[3] = {0, 0, 0};
    uint32_t                plane_widths[3] = {0, 0, 0};
    uint32_t                plane_heights[3] = {0, 0, 0};

    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer_ref;
    mutable std::mutex      payload_mu;
    mutable std::vector<uint8_t> payload;
};

struct librflow_stream_stats_s {
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

#endif  // __RFLOW_INTERNAL_FRAME_IMPL_H__

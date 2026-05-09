#ifndef __RFLOW_COMMON_MEDIA_FRAME_TYPES_H__
#define __RFLOW_COMMON_MEDIA_FRAME_TYPES_H__

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "common/abi/handle.h"
#include "api/scoped_refptr.h"
#include "api/video/video_frame_buffer.h"

using librflow_sampling_acquire_fn = rflow_err_t (*)(void* userdata);
using librflow_sampling_release_fn = void (*)(void* userdata);

struct librflow_video_frame_s {
    uint32_t magic;
    std::atomic<int32_t> refcount;

    rflow_video_frame_backend_t backend = RFLOW_VIDEO_FRAME_BACKEND_UNKNOWN;
    rflow_native_handle_type_t native_handle_type = RFLOW_NATIVE_HANDLE_NONE;
    rflow_codec_t codec;
    rflow_frame_type_t type;
    uint32_t width;
    uint32_t height;
    uint64_t pts_ms;
    uint64_t utc_ms;
    uint32_t seq;
    int32_t stream_index;

    uint32_t plane_count = 0;
    const uint8_t* plane_data[3] = {nullptr, nullptr, nullptr};
    uint32_t plane_strides[3] = {0, 0, 0};
    uint32_t plane_widths[3] = {0, 0, 0};
    uint32_t plane_heights[3] = {0, 0, 0};

    uint32_t oes_texture_id = 0;
    void* android_hardware_buffer = nullptr;
    int32_t sync_fence_fd = -1;
    librflow_gl_prepare_fn gl_prepare_fn = nullptr;
    void* gl_prepare_userdata = nullptr;
    librflow_sampling_acquire_fn sampling_acquire_fn = nullptr;
    librflow_sampling_release_fn sampling_release_fn = nullptr;
    void* sampling_userdata = nullptr;

    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer_ref;
    mutable std::mutex payload_mutex;
    mutable std::vector<uint8_t> payload;
};

struct librflow_stream_stats_s {
    uint32_t magic;
    std::atomic<int32_t> refcount;

    uint32_t duration_ms = 0;
    uint64_t in_bound_bytes = 0;
    uint64_t in_bound_pkts = 0;
    uint64_t out_bound_bytes = 0;
    uint64_t out_bound_pkts = 0;
    uint32_t lost_pkts = 0;
    uint32_t bitrate_kbps = 0;
    uint32_t rtt_ms = 0;
    uint32_t fps = 0;
    uint32_t jitter_ms = 0;
    uint32_t freeze_count = 0;
    uint32_t decode_fail_count = 0;
};

#endif  // __RFLOW_COMMON_MEDIA_FRAME_TYPES_H__

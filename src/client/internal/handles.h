/**
 * @file   handles.h
 * @brief  Client specific opaque handle definitions
 */

#ifndef __RFLOW_CLIENT_HANDLES_H__
#define __RFLOW_CLIENT_HANDLES_H__

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include "rflow/Client/librflow_client_api.h"
#include "common/internal/handle.h"

namespace rflow::client {

constexpr uint32_t kMagicConnectInfo = 0x52624349;   // 'RbCI'
constexpr uint32_t kMagicConnectCb   = 0x52624343;   // 'RbCC'
constexpr uint32_t kMagicStreamParam = 0x52625350;   // 'RbSP'
constexpr uint32_t kMagicStreamCb    = 0x52625343;   // 'RbSC'
constexpr uint32_t kMagicStream      = 0x52625348;   // 'RbSH'

}  // namespace rflow::client

struct librflow_connect_info_s {
    uint32_t    magic;
    std::string device_id;
    std::string device_secret;
};

struct librflow_connect_cb_s {
    uint32_t                       magic;
    librflow_on_connect_state_fn   on_state = nullptr;
    librflow_on_notice_fn          on_notice = nullptr;
    librflow_on_service_req_fn     on_service_req = nullptr;
    void*                          userdata = nullptr;
};

struct librflow_stream_param_s {
    uint32_t                    magic;
    rflow_codec_t               preferred_codec = RFLOW_CODEC_UNKNOWN;
    rflow_video_output_mode_t   video_output_mode = RFLOW_VIDEO_OUTPUT_MODE_CPU_PLANAR;
    uint32_t                    max_width = 0;
    uint32_t                    max_height = 0;
    uint32_t                    fps = 0;
    uint32_t                    open_timeout_ms = 0; /* 0 = SDK default policy */

    bool                        has_preferred_codec = false;
    bool                        has_video_output_mode = false;
    bool                        has_max_size = false;
    bool                        has_fps = false;
    bool                        has_open_timeout_ms = false;
};

struct librflow_stream_cb_s {
    uint32_t                       magic;
    librflow_on_stream_state_fn    on_state = nullptr;
    librflow_on_video_frame_fn     on_video = nullptr;
    librflow_on_stream_stats_fn    on_stream_stats = nullptr;
    void*                          userdata = nullptr;
};

struct librflow_stream_s {
    uint32_t                magic;
    int32_t                 index = 0;
    std::atomic<int32_t>    state{RFLOW_STREAM_IDLE};
    librflow_stream_cb_s    cb;
    std::chrono::steady_clock::time_point opened_at{};
    std::chrono::steady_clock::time_point last_stats_emit_at{};
    std::atomic<uint64_t>   video_frames_received{0};
    std::atomic<uint64_t>   last_stats_frames_received{0};

    std::shared_ptr<void>   impl;
};

#endif  // __RFLOW_CLIENT_HANDLES_H__

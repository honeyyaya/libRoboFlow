/**
 * @file   handles.h
 * @brief  Client 专属 opaque 对象的内部结构定义
**/

#ifndef __ROBRT_CLIENT_HANDLES_H__
#define __ROBRT_CLIENT_HANDLES_H__

#include <atomic>
#include <string>

#include "robrt/Client/librobrt_client_api.h"
#include "common/internal/handle.h"

namespace robrt::client {

constexpr uint32_t kMagicConnectInfo = 0x52624349;   // 'RbCI'
constexpr uint32_t kMagicConnectCb   = 0x52624343;   // 'RbCC'
constexpr uint32_t kMagicStreamParam = 0x52625350;   // 'RbSP'
constexpr uint32_t kMagicStreamCb    = 0x52625343;   // 'RbSC'
constexpr uint32_t kMagicStream      = 0x52625348;   // 'RbSH'

}  // namespace robrt::client

struct librobrt_connect_info_s {
    uint32_t    magic;
    std::string device_id;
    std::string device_secret;
};

struct librobrt_connect_cb_s {
    uint32_t                       magic;
    librobrt_on_connect_state_fn   on_state;
    librobrt_on_notice_fn          on_notice;
    librobrt_on_service_req_fn     on_service_req;
    librobrt_on_stream_stats_fn    on_stream_stats;
    void*                          userdata;
};

struct librobrt_stream_param_s {
    uint32_t        magic;
    robrt_codec_t   preferred_codec;
    uint32_t        max_width;
    uint32_t        max_height;
    uint32_t        fps;
    bool            enable_audio;
};

struct librobrt_stream_cb_s {
    uint32_t                       magic;
    librobrt_on_stream_state_fn    on_state;
    librobrt_on_video_frame_fn     on_video;
    librobrt_on_audio_frame_fn     on_audio;
    void*                          userdata;
};

struct librobrt_stream_s {
    uint32_t                magic;
    int32_t                 index;
    std::atomic<int32_t>    state;  // robrt_stream_state_t
    librobrt_stream_cb_s    cb;     // 按值保存
};

#endif  // __ROBRT_CLIENT_HANDLES_H__

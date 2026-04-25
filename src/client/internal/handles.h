/**
 * @file   handles.h
 * @brief  Client 专属 opaque 对象的内部结构定义
**/

#ifndef __RFLOW_CLIENT_HANDLES_H__
#define __RFLOW_CLIENT_HANDLES_H__

#include <atomic>
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
    librflow_on_connect_state_fn   on_state;
    librflow_on_notice_fn          on_notice;
    librflow_on_service_req_fn     on_service_req;
    void*                          userdata;
};

struct librflow_stream_param_s {
    uint32_t        magic;
    rflow_codec_t   preferred_codec;
    rflow_video_output_mode_t video_output_mode;
    uint32_t        max_width;
    uint32_t        max_height;
    uint32_t        fps;
    uint32_t        open_timeout_ms; /* 0 = SDK 默认策略 */

    /* 追踪每个字段是否被 setter 显式赋值过：
     * 用于区分 "未设置" 与 "显式设置为 0/UNKNOWN"，支撑 getter 返回 NOT_FOUND 语义。 */
    bool            has_preferred_codec;
    bool            has_video_output_mode;
    bool            has_max_size;
    bool            has_fps;
    bool            has_open_timeout_ms;
};

struct librflow_stream_cb_s {
    uint32_t                       magic;
    librflow_on_stream_state_fn    on_state;
    librflow_on_video_frame_fn     on_video;
    librflow_on_stream_stats_fn    on_stream_stats;
    void*                          userdata;
};

struct librflow_stream_s {
    uint32_t                magic;
    int32_t                 index;
    std::atomic<int32_t>    state;  // rflow_stream_state_t
    librflow_stream_cb_s    cb;     // 按值保存

    /* 底层实现对象（当前仅 WebRTC 构建使用：
     *   shared_ptr<rflow::client::impl::RtcStreamSession>）。
     * 用 void 擦除类型以避免公共 handles.h 强依赖 impl 头。
     * 生命周期：open_stream 赋值 → close_stream / disconnect 释放。 */
    std::shared_ptr<void>   impl;
};

#endif  // __RFLOW_CLIENT_HANDLES_H__

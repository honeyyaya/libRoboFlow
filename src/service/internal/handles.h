#ifndef __RFLOW_SERVICE_HANDLES_H__
#define __RFLOW_SERVICE_HANDLES_H__

#include <atomic>
#include <string>
#include <vector>

#include "rflow/Service/librflow_service_api.h"
#include "common/internal/handle.h"

namespace rflow::service {

constexpr uint32_t kMagicConnectInfo = 0x52735349;  // 'RsSI'
constexpr uint32_t kMagicConnectCb   = 0x52735343;  // 'RsSC'
constexpr uint32_t kMagicStreamParam = 0x52735350;  // 'RsSP'
constexpr uint32_t kMagicStreamCb    = 0x52734342;  // 'RsCB'
constexpr uint32_t kMagicStream      = 0x52735348;  // 'RsSH'
constexpr uint32_t kMagicPushFrame   = 0x52735046;  // 'RsPF'
constexpr uint32_t kMagicLicenseInfo = 0x52734C49;  // 'RsLI'

}  // namespace rflow::service

struct librflow_svc_connect_info_s {
    uint32_t    magic;
    std::string device_id;
    std::string device_secret;
    std::string product_key;
    std::string vendor_id;
};

struct librflow_svc_connect_cb_s {
    uint32_t                           magic;
    librflow_svc_on_connect_state_fn   on_state;
    librflow_svc_on_bind_state_fn      on_bind_state;
    librflow_svc_on_notice_fn          on_notice;
    librflow_svc_on_service_req_fn     on_service_req;
    librflow_svc_on_pull_request_fn    on_pull_request;
    librflow_svc_on_pull_release_fn    on_pull_release;
    void*                              userdata;
};

struct librflow_svc_stream_param_s {
    uint32_t           magic;
    rflow_codec_t      in_codec;
    rflow_codec_t      out_codec;
    uint32_t           src_w, src_h;
    uint32_t           out_w, out_h;
    uint32_t           fps;
    uint32_t           gop;
    rflow_rc_mode_t    rc_mode;
    uint32_t           qp;
    uint32_t           bitrate_kbps;
    uint32_t           max_bitrate_kbps;
    bool               dynamic_bitrate;
    uint32_t           lowest_kbps;
    uint32_t           highest_kbps;
    bool               enable_transcode;

    /* 追踪每个字段是否被 setter 显式赋值过（NOT_FOUND vs 显式 0 的消歧） */
    bool               has_in_codec;
    bool               has_out_codec;
    bool               has_src_size;
    bool               has_out_size;
    bool               has_fps;
    bool               has_gop;
    bool               has_rc_mode;
    bool               has_qp;
    bool               has_bitrate;
    bool               has_dynamic_bitrate;
    bool               has_enable_transcode;
};

struct librflow_svc_stream_cb_s {
    uint32_t                           magic;
    librflow_svc_on_stream_state_fn    on_state;
    librflow_svc_on_encoded_video_fn   on_encoded_video;
    librflow_svc_on_stream_stats_fn    on_stream_stats;
    void*                              userdata;
};

struct librflow_svc_stream_s {
    uint32_t                     magic;
    int32_t                      stream_idx;
    std::atomic<int32_t>         state;
    librflow_svc_stream_param_s  param;
    librflow_svc_stream_cb_s     cb;
    bool                         started;
};

struct librflow_svc_push_frame_s {
    uint32_t             magic;
    rflow_codec_t        codec;
    rflow_frame_type_t   type;
    std::vector<uint8_t> data;
    uint32_t             width, height;
    uint64_t             pts_ms;
    uint64_t             utc_ms;
    uint32_t             seq;
    bool                 flush;
    uint32_t             offset;

    /* 追踪每个字段是否被 setter 显式赋值过 */
    bool                 has_codec;
    bool                 has_type;
    bool                 has_data;
    bool                 has_size;
    bool                 has_pts_ms;
    bool                 has_utc_ms;
    bool                 has_seq;
    bool                 has_flush;
    bool                 has_offset;
};

struct librflow_svc_license_info_s {
    uint32_t    magic;
    uint64_t    expire_time_sec;  /* Unix epoch (UTC), 0 = 永久授权 */
    bool        loaded;           /* false = license 未成功加载 */
    std::string vendor_id;
    std::string product_key;
};

#endif  // __RFLOW_SERVICE_HANDLES_H__

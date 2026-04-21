#ifndef __ROBRT_SERVICE_HANDLES_H__
#define __ROBRT_SERVICE_HANDLES_H__

#include <atomic>
#include <string>
#include <vector>

#include "robrt/Service/librobrt_service_api.h"
#include "common/internal/handle.h"

namespace robrt::service {

constexpr uint32_t kMagicConnectInfo = 0x52735349;  // 'RsSI'
constexpr uint32_t kMagicConnectCb   = 0x52735343;  // 'RsSC'
constexpr uint32_t kMagicStreamParam = 0x52735350;  // 'RsSP'
constexpr uint32_t kMagicStreamCb    = 0x52734342;  // 'RsCB'
constexpr uint32_t kMagicStream      = 0x52735348;  // 'RsSH'
constexpr uint32_t kMagicPushFrame   = 0x52735046;  // 'RsPF'
constexpr uint32_t kMagicTalkConfig  = 0x52735443;  // 'RsTC'
constexpr uint32_t kMagicLicenseInfo = 0x52734C49;  // 'RsLI'

}  // namespace robrt::service

struct librobrt_svc_connect_info_s {
    uint32_t    magic;
    std::string device_id;
    std::string device_secret;
    std::string product_key;
    std::string vendor_id;
};

struct librobrt_svc_connect_cb_s {
    uint32_t                           magic;
    librobrt_svc_on_connect_state_fn   on_state;
    librobrt_svc_on_bind_state_fn      on_bind_state;
    librobrt_svc_on_notice_fn          on_notice;
    librobrt_svc_on_service_req_fn     on_service_req;
    librobrt_svc_on_pull_request_fn    on_pull_request;
    librobrt_svc_on_pull_release_fn    on_pull_release;
    librobrt_svc_on_talk_start_fn      on_talk_start;
    librobrt_svc_on_talk_stop_fn       on_talk_stop;
    librobrt_svc_on_talk_audio_fn      on_talk_audio;
    librobrt_svc_on_talk_video_fn      on_talk_video;
    librobrt_svc_on_stream_stats_fn    on_stream_stats;
    void*                              userdata;
};

struct librobrt_svc_stream_param_s {
    uint32_t           magic;
    robrt_codec_t      in_codec;
    robrt_codec_t      out_codec;
    uint32_t           src_w, src_h;
    uint32_t           out_w, out_h;
    uint32_t           fps;
    uint32_t           gop;
    robrt_rc_mode_t    rc_mode;
    uint32_t           qp;
    uint32_t           bitrate_kbps;
    uint32_t           max_bitrate_kbps;
    bool               dynamic_bitrate;
    uint32_t           lowest_kbps;
    uint32_t           highest_kbps;
    bool               enable_transcode;
    bool               enable_audio;
    robrt_audio_codec_t audio_codec;
    uint32_t           audio_sample_rate;
    uint32_t           audio_channel;
    uint32_t           audio_sample_bit;
};

struct librobrt_svc_stream_cb_s {
    uint32_t                           magic;
    librobrt_svc_on_stream_state_fn    on_state;
    librobrt_svc_on_encoded_video_fn   on_encoded_video;
    void*                              userdata;
};

struct librobrt_svc_stream_s {
    uint32_t                     magic;
    int32_t                      stream_idx;
    std::atomic<int32_t>         state;
    librobrt_svc_stream_param_s  param;
    librobrt_svc_stream_cb_s     cb;
    bool                         started;
};

struct librobrt_svc_push_frame_s {
    uint32_t             magic;
    robrt_codec_t        codec;
    robrt_frame_type_t   type;
    std::vector<uint8_t> data;
    uint32_t             width, height;
    uint64_t             pts_ms;
    uint64_t             utc_ms;
    uint32_t             seq;
    bool                 flush;
    uint32_t             offset;
};

struct librobrt_svc_talk_config_s {
    uint32_t             magic;
    robrt_audio_codec_t  audio_codec;
    uint32_t             audio_sample_rate;
    uint32_t             audio_channel;
    uint32_t             audio_sample_bit;
    robrt_codec_t        video_codec;
    uint32_t             video_max_bitrate_kbps;
};

struct librobrt_svc_license_info_s {
    uint32_t    magic;
    uint32_t    expire_time;
    std::string vendor_id;
    std::string product_key;
};

#endif  // __ROBRT_SERVICE_HANDLES_H__

/**
 * @file     librobrt_service_api.h
 * @brief    Service 端对外 API（纯 C ABI，opaque handle + get/set）
 *
 * Service 角色：设备侧媒体发布端，内部封装 WebRTC 和编码器。
 *   - 业务层通过 push_video_frame / push_audio_frame 将采集数据送入 SDK；
 *     SDK 按 stream_param 指定的目标编码格式进行编码/透传/转码，并推送给订阅端；
 *   - 也可以接收 Client 侧的对讲 (talk) 音视频，通过 on_talk_* 回调交给业务层播放；
 *   - 处理 Client 侧的业务服务请求：on_service_req + librobrt_svc_service_reply；
 *   - 处理 Client 侧的订阅行为：on_pull_request / on_pull_release 通知业务层
 *     某路流何时需要开始/停止产生数据。
 *
 * 共享部分（log/signal/license/global config、video_frame、audio_frame、
 * stream_stats 的 getter、内存释放、错误与版本）见 librobrt_common.h。
 *
 * 调用时序：
 *   set_global_config -> init -> connect ->
 *     create_stream -> start_stream -> [push_video_frame / push_audio_frame]* -> stop_stream -> destroy_stream
 *   -> disconnect -> uninit
 *   disconnect / uninit / destroy_stream 均幂等强清理。
**/

#ifndef __LIBROBRT_SERVICE_API_H__
#define __LIBROBRT_SERVICE_API_H__

#include "../librobrt_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *                          Service 专属枚举
 ******************************************************************************/

/* 设备绑定状态 */
typedef enum {
    ROBRT_BIND_UNBOUND   = 0,
    ROBRT_BIND_BOUND     = 1,
    ROBRT_BIND_FAILED    = 2,
} robrt_bind_state_t;

/* 流传输方向 */
typedef enum {
    ROBRT_DIR_UNKNOWN = 0,
    ROBRT_DIR_PUBLISH = 1,  /* Service -> Client（下行，主媒体流） */
    ROBRT_DIR_TALK    = 2,  /* Client  -> Service（上行对讲） */
} robrt_stream_dir_t;

/******************************************************************************
 *                          Opaque Handles — Service 专属
 ******************************************************************************/
typedef struct librobrt_svc_connect_info_s*  librobrt_svc_connect_info_t;
typedef struct librobrt_svc_connect_cb_s*    librobrt_svc_connect_cb_t;
typedef struct librobrt_svc_stream_param_s*  librobrt_svc_stream_param_t;
typedef struct librobrt_svc_stream_cb_s*     librobrt_svc_stream_cb_t;
typedef struct librobrt_svc_stream_s*        librobrt_svc_stream_handle_t;
typedef struct librobrt_svc_push_frame_s*    librobrt_svc_push_frame_t;   /* 业务向 SDK 推送的帧 */
typedef struct librobrt_svc_talk_config_s*   librobrt_svc_talk_config_t;  /* 对讲参数配置 */
typedef struct librobrt_svc_license_info_s*  librobrt_svc_license_info_t; /* license 信息只读对象 */

/******************************************************************************
 *                          ConnectInfo（设备身份 + 产品信息）
 ******************************************************************************/
LIBROBRT_API_EXPORT librobrt_svc_connect_info_t librobrt_svc_connect_info_create(void);
LIBROBRT_API_EXPORT void                        librobrt_svc_connect_info_destroy(librobrt_svc_connect_info_t info);

LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_info_set_device_id    (librobrt_svc_connect_info_t info, const char *device_id);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_info_set_device_secret(librobrt_svc_connect_info_t info, const char *device_secret);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_info_set_product_key  (librobrt_svc_connect_info_t info, const char *product_key);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_info_set_vendor_id    (librobrt_svc_connect_info_t info, const char *vid);

/******************************************************************************
 *                          ConnectCb
 ******************************************************************************/

/** 连接状态变化 */
typedef void (*librobrt_svc_on_connect_state_fn)(robrt_connect_state_t state,
                                                 robrt_err_t reason,
                                                 void *userdata);

/** 设备绑定状态变化；extra 为可选文本（失败原因等），仅回调栈内有效 */
typedef void (*librobrt_svc_on_bind_state_fn)(robrt_bind_state_t state,
                                              const char *extra,
                                              void *userdata);

/** 通知类消息（来自 Client，单向） */
typedef void (*librobrt_svc_on_notice_fn)(int32_t index,
                                          const void *payload,
                                          uint32_t len,
                                          void *userdata);

/**
 * 业务请求（来自 Client，需要回包）
 * 业务处理完成后调用 librobrt_svc_service_reply(req_id, ...)。
 * 超时未回包，SDK 自动以 ROBRT_ERR_TIMEOUT 应答。
 */
typedef void (*librobrt_svc_on_service_req_fn)(uint64_t req_id,
                                               const char *service_id,
                                               const void *payload,
                                               uint32_t len,
                                               void *userdata);

/**
 * Client 侧请求订阅某路流（pull）
 * SDK 触发此回调后，业务层应当保证 stream_idx 对应的流已 create_stream + start_stream，
 * 并开始 push_video_frame / push_audio_frame。
 * 返回 void，业务处理失败通过 state 回调体现（不回帧即可）。
 */
typedef void (*librobrt_svc_on_pull_request_fn)(int32_t stream_idx,
                                                void *userdata);

/** Client 侧不再订阅该路流（最后一个订阅者离开） */
typedef void (*librobrt_svc_on_pull_release_fn)(int32_t stream_idx,
                                                void *userdata);

/** 对讲开始：Client 端发起对讲，业务层准备播放 */
typedef void (*librobrt_svc_on_talk_start_fn)(int32_t stream_idx,
                                              void *userdata);

/** 对讲停止 */
typedef void (*librobrt_svc_on_talk_stop_fn)(int32_t stream_idx,
                                             void *userdata);

/** 对讲音频帧到达；frame 仅回调栈内有效，跨栈持有用 librobrt_audio_frame_retain */
typedef void (*librobrt_svc_on_talk_audio_fn)(int32_t stream_idx,
                                              librobrt_audio_frame_t frame,
                                              void *userdata);

/** 对讲视频帧到达（如果开启了视频对讲） */
typedef void (*librobrt_svc_on_talk_video_fn)(int32_t stream_idx,
                                              librobrt_video_frame_t frame,
                                              void *userdata);

/** 下行流统计（SDK 内部周期上报） */
typedef void (*librobrt_svc_on_stream_stats_fn)(librobrt_svc_stream_handle_t handle,
                                                librobrt_stream_stats_t stats,
                                                void *userdata);

LIBROBRT_API_EXPORT librobrt_svc_connect_cb_t librobrt_svc_connect_cb_create(void);
LIBROBRT_API_EXPORT void                      librobrt_svc_connect_cb_destroy(librobrt_svc_connect_cb_t cb);

LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_cb_set_on_state        (librobrt_svc_connect_cb_t cb, librobrt_svc_on_connect_state_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_cb_set_on_bind_state   (librobrt_svc_connect_cb_t cb, librobrt_svc_on_bind_state_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_cb_set_on_notice       (librobrt_svc_connect_cb_t cb, librobrt_svc_on_notice_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_cb_set_on_service_req  (librobrt_svc_connect_cb_t cb, librobrt_svc_on_service_req_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_cb_set_on_pull_request (librobrt_svc_connect_cb_t cb, librobrt_svc_on_pull_request_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_cb_set_on_pull_release (librobrt_svc_connect_cb_t cb, librobrt_svc_on_pull_release_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_cb_set_on_talk_start   (librobrt_svc_connect_cb_t cb, librobrt_svc_on_talk_start_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_cb_set_on_talk_stop    (librobrt_svc_connect_cb_t cb, librobrt_svc_on_talk_stop_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_cb_set_on_talk_audio   (librobrt_svc_connect_cb_t cb, librobrt_svc_on_talk_audio_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_cb_set_on_talk_video   (librobrt_svc_connect_cb_t cb, librobrt_svc_on_talk_video_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_cb_set_on_stream_stats (librobrt_svc_connect_cb_t cb, librobrt_svc_on_stream_stats_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect_cb_set_userdata        (librobrt_svc_connect_cb_t cb, void *userdata);

/******************************************************************************
 *                          StreamParam（下行编码流参数）
 ******************************************************************************/
LIBROBRT_API_EXPORT librobrt_svc_stream_param_t librobrt_svc_stream_param_create(void);
LIBROBRT_API_EXPORT void                        librobrt_svc_stream_param_destroy(librobrt_svc_stream_param_t p);

/* 输入视频编码：业务 push 进来的原始或已编码帧类型 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_param_set_in_codec (librobrt_svc_stream_param_t p, robrt_codec_t codec);
/* 输出视频编码：SDK 发布给订阅端的编码类型；与输入相同且未开启 transcode 时透传 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_param_set_out_codec(librobrt_svc_stream_param_t p, robrt_codec_t codec);
/* 输入分辨率（源） */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_param_set_src_size (librobrt_svc_stream_param_t p, uint32_t width, uint32_t height);
/* 输出分辨率 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_param_set_out_size (librobrt_svc_stream_param_t p, uint32_t width, uint32_t height);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_param_set_fps      (librobrt_svc_stream_param_t p, uint32_t fps);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_param_set_gop      (librobrt_svc_stream_param_t p, uint32_t gop_size);

/* 码率控制 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_param_set_rc_mode  (librobrt_svc_stream_param_t p, robrt_rc_mode_t rc);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_param_set_qp       (librobrt_svc_stream_param_t p, uint32_t qp);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_param_set_bitrate  (librobrt_svc_stream_param_t p, uint32_t bitrate_kbps, uint32_t max_bitrate_kbps);

/* 动态码率：启用时按 [min, max] 动态调整 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_param_set_dynamic_bitrate(librobrt_svc_stream_param_t p,
                                                                              bool     enable,
                                                                              uint32_t lowest_kbps,
                                                                              uint32_t highest_kbps);

/* 输入/输出 codec 相同且不启用，SDK 透传；启用时即使相同 codec 也会强制转码 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_param_set_enable_transcode(librobrt_svc_stream_param_t p, bool enable);

/* 是否附带音频 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_param_set_enable_audio(librobrt_svc_stream_param_t p, bool enable);
/* 音频参数（enable_audio = true 时生效） */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_param_set_audio(librobrt_svc_stream_param_t p,
                                                                    robrt_audio_codec_t codec,
                                                                    uint32_t sample_rate,
                                                                    uint32_t channel,
                                                                    uint32_t sample_bit);

/******************************************************************************
 *                          StreamCb（流本地观测回调，可选）
 ******************************************************************************/

/** 流状态变化 */
typedef void (*librobrt_svc_on_stream_state_fn)(librobrt_svc_stream_handle_t handle,
                                                robrt_stream_state_t state,
                                                robrt_err_t reason,
                                                void *userdata);

/** 编码后视频帧（本地观测用，例如落盘/录像）；frame 仅回调栈内有效 */
typedef void (*librobrt_svc_on_encoded_video_fn)(librobrt_svc_stream_handle_t handle,
                                                 librobrt_video_frame_t frame,
                                                 void *userdata);

LIBROBRT_API_EXPORT librobrt_svc_stream_cb_t librobrt_svc_stream_cb_create(void);
LIBROBRT_API_EXPORT void                     librobrt_svc_stream_cb_destroy(librobrt_svc_stream_cb_t cb);

LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_cb_set_on_state         (librobrt_svc_stream_cb_t cb, librobrt_svc_on_stream_state_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_cb_set_on_encoded_video (librobrt_svc_stream_cb_t cb, librobrt_svc_on_encoded_video_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_cb_set_userdata         (librobrt_svc_stream_cb_t cb, void *userdata);

/******************************************************************************
 *                          PushFrame（业务 → SDK 推帧对象，避免多参函数）
 ******************************************************************************/
LIBROBRT_API_EXPORT librobrt_svc_push_frame_t librobrt_svc_push_frame_create(void);
LIBROBRT_API_EXPORT void                      librobrt_svc_push_frame_destroy(librobrt_svc_push_frame_t f);

/* 必填 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_push_frame_set_codec   (librobrt_svc_push_frame_t f, robrt_codec_t codec);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_push_frame_set_type    (librobrt_svc_push_frame_t f, robrt_frame_type_t type);
/* SDK 内部 copy data，调用返回后业务可立即释放 buffer */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_push_frame_set_data    (librobrt_svc_push_frame_t f,
                                                                      const void *data, uint32_t size);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_push_frame_set_size    (librobrt_svc_push_frame_t f, uint32_t width, uint32_t height);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_push_frame_set_pts_ms  (librobrt_svc_push_frame_t f, uint64_t pts_ms);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_push_frame_set_utc_ms  (librobrt_svc_push_frame_t f, uint64_t utc_ms);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_push_frame_set_seq     (librobrt_svc_push_frame_t f, uint32_t seq);

/* 可选：分片推送（大帧切块）；flush = true 表示当前帧已经推完 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_push_frame_set_flush   (librobrt_svc_push_frame_t f, bool flush);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_push_frame_set_offset  (librobrt_svc_push_frame_t f, uint32_t offset);

/******************************************************************************
 *                          TalkConfig（对讲参数，用于告诉 SDK 业务能处理哪些对讲格式）
 ******************************************************************************/
LIBROBRT_API_EXPORT librobrt_svc_talk_config_t librobrt_svc_talk_config_create(void);
LIBROBRT_API_EXPORT void                       librobrt_svc_talk_config_destroy(librobrt_svc_talk_config_t cfg);

/* 业务支持的对讲音频格式 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_talk_config_set_audio(librobrt_svc_talk_config_t cfg,
                                                                    robrt_audio_codec_t codec,
                                                                    uint32_t sample_rate,
                                                                    uint32_t channel,
                                                                    uint32_t sample_bit);
/* 业务支持的对讲视频编码及最大码率 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_talk_config_set_video(librobrt_svc_talk_config_t cfg,
                                                                    robrt_codec_t codec,
                                                                    uint32_t max_bitrate_kbps);

/******************************************************************************
 *                          LicenseInfo（只读对象，getter）
 ******************************************************************************/
LIBROBRT_API_EXPORT uint32_t librobrt_svc_license_info_get_expire_time(librobrt_svc_license_info_t info);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_license_info_get_vendor_id  (librobrt_svc_license_info_t info, char *buf, uint32_t buf_len);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_license_info_get_product_key(librobrt_svc_license_info_t info, char *buf, uint32_t buf_len);
LIBROBRT_API_EXPORT void     librobrt_svc_license_info_destroy(librobrt_svc_license_info_t info);

/******************************************************************************
 *                          生命周期 API
 ******************************************************************************/

/** 设置全局配置；必须在 librobrt_svc_init 之前调用 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_set_global_config(librobrt_global_config_t cfg);

/** 设置对讲配置；可在 init 前或 connect 前调用；一旦连接建立不可修改 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_set_talk_config(librobrt_svc_talk_config_t cfg);

/** 初始化（线程池 / SSL / WebRTC 栈 / 编码器后端）；幂等 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_init(void);

/** 反初始化；幂等强清理：内部自动 disconnect + 销毁所有 stream */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_uninit(void);

/** 注册设备到服务端 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_connect(librobrt_svc_connect_info_t info,
                                                     librobrt_svc_connect_cb_t   cb);

/** 断开连接；幂等强清理 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_disconnect(void);

/** 获取当前 license 信息；返回的 info 需调用方 destroy */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_get_license_info(librobrt_svc_license_info_t *out_info);

/******************************************************************************
 *                          流管理
 ******************************************************************************/

/**
 * 创建一路发布流
 * @param stream_idx  业务协议中的资源 ID（与 Client 的 index 一一对应）
 * @param param       编码参数（必填）
 * @param cb          流回调（可为 NULL，表示不关心本地观测）
 * @param out_handle  输出流句柄
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_create_stream(int32_t stream_idx,
                                                           librobrt_svc_stream_param_t param,
                                                           librobrt_svc_stream_cb_t    cb,
                                                           librobrt_svc_stream_handle_t *out_handle);

/** 开始发布流（此后 push_*_frame 才会真正参与编码/发送） */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_start_stream(librobrt_svc_stream_handle_t handle);

/** 停止发布流（保留编码器状态，可再次 start） */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stop_stream (librobrt_svc_stream_handle_t handle);

/** 销毁发布流；幂等 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_destroy_stream(librobrt_svc_stream_handle_t handle);

/** 推送一帧视频；SDK 内部 copy 数据后立即返回 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_push_video_frame(librobrt_svc_stream_handle_t handle,
                                                              librobrt_svc_push_frame_t frame);

/** 推送一帧音频 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_push_audio_frame(librobrt_svc_stream_handle_t handle,
                                                              librobrt_svc_push_frame_t frame);

/** 强制下一帧编码为关键帧（用于对订阅端丢帧恢复） */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_force_keyframe(librobrt_svc_stream_handle_t handle);

/** 运行期调整目标码率（kbps） */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_set_bitrate(librobrt_svc_stream_handle_t handle,
                                                                uint32_t bitrate_kbps);

/** 主动拉取一次流统计 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_stream_get_stats(librobrt_svc_stream_handle_t handle,
                                                              librobrt_stream_stats_t *out_stats);

/******************************************************************************
 *                          信令消息收发
 ******************************************************************************/

/** 主动向 Client/云端推送通知（fire-and-forget） */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_send_notice(int32_t index,
                                                         const void *payload,
                                                         uint32_t len);

/**
 * 发布 topic 消息（透传通道，适用于高频状态类上报）
 * 与 send_notice 的区别：不经过业务语义封装，内部直接透传
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_post_topic (const char *topic,
                                                         const void *payload,
                                                         uint32_t len);

/** 业务层对 Client request 的回包 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_service_reply(uint64_t req_id,
                                                           robrt_err_t status,
                                                           const void *payload,
                                                           uint32_t len);

/******************************************************************************
 *                          运行期日志 setter（init 后生效）
 ******************************************************************************/
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_log_set_level   (robrt_log_level_t level);
LIBROBRT_API_EXPORT robrt_err_t librobrt_svc_log_set_callback(librobrt_log_cb_fn cb, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* __LIBROBRT_SERVICE_API_H__ */

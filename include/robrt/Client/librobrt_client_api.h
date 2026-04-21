/**
 * @file     librobrt_client_api.h
 * @brief    Client 端对外 API（纯 C ABI，opaque handle + get/set）
 *
 * Client 角色：订阅远端 Service 的媒体流，接收视频数据（音频暂不支持）。
 *   - 仅负责"拉"，不做采集/编码；
 *   - 编码/分辨率/帧率等参数由 Service 侧决定，Client 只能给出 hint；
 *   - 通过 on_video_frame 把帧回调给业务层。
 *
 * 共享部分（log/signal/license/global config、video_frame、
 * stream_stats 的 getter、内存释放、错误与版本）见 librobrt_common.h。
 *
 * 调用时序：
 *   set_global_config -> init -> connect -> open_stream* -> close_stream* -> disconnect -> uninit
 *   disconnect / uninit / close_stream 均幂等强清理。
**/

#ifndef __LIBROBRT_CLIENT_API_H__
#define __LIBROBRT_CLIENT_API_H__

#include "../librobrt_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *                          Opaque Handles — Client 专属
 ******************************************************************************/
typedef struct librobrt_connect_info_s*    librobrt_connect_info_t;
typedef struct librobrt_connect_cb_s*      librobrt_connect_cb_t;
typedef struct librobrt_stream_param_s*    librobrt_stream_param_t;
typedef struct librobrt_stream_cb_s*       librobrt_stream_cb_t;
typedef struct librobrt_stream_s*          librobrt_stream_handle_t;

/******************************************************************************
 *                          ConnectInfo
 ******************************************************************************/
LIBROBRT_API_EXPORT librobrt_connect_info_t librobrt_connect_info_create(void);
LIBROBRT_API_EXPORT void                    librobrt_connect_info_destroy(librobrt_connect_info_t info);

LIBROBRT_API_EXPORT robrt_err_t librobrt_connect_info_set_device_id    (librobrt_connect_info_t info, const char *device_id);
LIBROBRT_API_EXPORT robrt_err_t librobrt_connect_info_set_device_secret(librobrt_connect_info_t info, const char *device_secret);

/******************************************************************************
 *                          ConnectCb
 ******************************************************************************/

/**
 * 连接状态变化
 * @param reason 若 state = FAILED/DISCONNECTED，为原因错误码；否则 ROBRT_OK
 */
typedef void (*librobrt_on_connect_state_fn)(robrt_connect_state_t state,
                                             robrt_err_t reason,
                                             void *userdata);

/** 通知类消息（单向，不要求回包） */
typedef void (*librobrt_on_notice_fn)(int32_t index,
                                      const void *payload,
                                      uint32_t len,
                                      void *userdata);

/**
 * 请求类服务消息（需要回包）
 * @param req_id   SDK 分配的请求 ID，用于 librobrt_service_reply 异步回包
 * 超时未回包时，SDK 自动以 ROBRT_ERR_TIMEOUT 应答并释放 req_id。
 */
typedef void (*librobrt_on_service_req_fn)(uint64_t req_id,
                                           const char *service_id,
                                           const void *payload,
                                           uint32_t len,
                                           void *userdata);

LIBROBRT_API_EXPORT librobrt_connect_cb_t librobrt_connect_cb_create(void);
LIBROBRT_API_EXPORT void                  librobrt_connect_cb_destroy(librobrt_connect_cb_t cb);

LIBROBRT_API_EXPORT robrt_err_t librobrt_connect_cb_set_on_state       (librobrt_connect_cb_t cb, librobrt_on_connect_state_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_connect_cb_set_on_notice      (librobrt_connect_cb_t cb, librobrt_on_notice_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_connect_cb_set_on_service_req (librobrt_connect_cb_t cb, librobrt_on_service_req_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_connect_cb_set_userdata       (librobrt_connect_cb_t cb, void *userdata);

/******************************************************************************
 *                          StreamParam（订阅偏好，非强约束 hint）
 ******************************************************************************/
LIBROBRT_API_EXPORT librobrt_stream_param_t librobrt_stream_param_create(void);
LIBROBRT_API_EXPORT void                    librobrt_stream_param_destroy(librobrt_stream_param_t p);

LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_param_set_preferred_codec   (librobrt_stream_param_t p, robrt_codec_t codec);
LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_param_set_preferred_max_size(librobrt_stream_param_t p,
                                                                             uint32_t max_width,
                                                                             uint32_t max_height);
LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_param_set_preferred_fps     (librobrt_stream_param_t p, uint32_t fps);

/******************************************************************************
 *                          StreamCb
 ******************************************************************************/

/** 流状态变化；reason 在异常状态时为原因错误码 */
typedef void (*librobrt_on_stream_state_fn)(librobrt_stream_handle_t handle,
                                            robrt_stream_state_t state,
                                            robrt_err_t reason,
                                            void *userdata);

/** 视频帧到达；frame 仅回调栈内有效，跨栈持有用 librobrt_video_frame_retain */
typedef void (*librobrt_on_video_frame_fn)(librobrt_stream_handle_t handle,
                                           librobrt_video_frame_t frame,
                                           void *userdata);

/**
 * 流统计（SDK 周期 push）；stats 仅回调栈内有效。
 * 不显式传 handle — 表示为本路 open_stream 所绑定 stream_cb 对应之流。
 */
typedef void (*librobrt_on_stream_stats_fn)(librobrt_stream_stats_t stats,
                                            void *userdata);

LIBROBRT_API_EXPORT librobrt_stream_cb_t librobrt_stream_cb_create(void);
LIBROBRT_API_EXPORT void                 librobrt_stream_cb_destroy(librobrt_stream_cb_t cb);

LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_cb_set_on_state (librobrt_stream_cb_t cb, librobrt_on_stream_state_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_cb_set_on_video (librobrt_stream_cb_t cb, librobrt_on_video_frame_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_cb_set_on_stream_stats(librobrt_stream_cb_t cb, librobrt_on_stream_stats_fn fn);
LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_cb_set_userdata (librobrt_stream_cb_t cb, void *userdata);

/******************************************************************************
 *                          生命周期 API
 ******************************************************************************/

/** 设置全局配置；必须在 librobrt_init 之前调用 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_set_global_config(librobrt_global_config_t cfg);

/** 初始化（线程池 / SSL / WebRTC 栈等）；幂等 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_init(void);

/** 反初始化；幂等强清理：内部自动 disconnect + 关闭所有 stream */
LIBROBRT_API_EXPORT robrt_err_t librobrt_uninit(void);

/** 连接设备到服务端；一个进程仅允许单设备连接 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_connect(librobrt_connect_info_t connect_info,
                                                 librobrt_connect_cb_t   connect_cb);

/** 断开连接；幂等强清理：内部自动关闭所有未 close 的 stream */
LIBROBRT_API_EXPORT robrt_err_t librobrt_disconnect(void);

/**
 * 打开订阅流
 * @param index       业务协议中的资源 ID
 * @param param       订阅偏好（可为 NULL，使用默认）
 * @param cb          流回调（video/state/可选 stats）
 * @param out_handle  输出流句柄
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_open_stream(int32_t index,
                                                     librobrt_stream_param_t param,
                                                     librobrt_stream_cb_t    cb,
                                                     librobrt_stream_handle_t *out_handle);

/** 关闭订阅流；幂等 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_close_stream(librobrt_stream_handle_t handle);

/** 主动拉取一次流统计；stats 仅调用栈内有效 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_get_stats(librobrt_stream_handle_t handle,
                                                          librobrt_stream_stats_t *out_stats);

/******************************************************************************
 *                          信令消息收发
 ******************************************************************************/

/** 主动向 Service 发送业务通知（fire-and-forget） */
LIBROBRT_API_EXPORT robrt_err_t librobrt_send_notice(int32_t index,
                                                     const void *payload,
                                                     uint32_t len);

/** 业务层对 Service request 的回包 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_service_reply(uint64_t req_id,
                                                       robrt_err_t status,
                                                       const void *payload,
                                                       uint32_t len);

/******************************************************************************
 *                          运行期日志 setter（init 后生效）
 ******************************************************************************/
LIBROBRT_API_EXPORT robrt_err_t librobrt_log_set_level   (robrt_log_level_t level);
LIBROBRT_API_EXPORT robrt_err_t librobrt_log_set_callback(librobrt_log_cb_fn cb, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* __LIBROBRT_CLIENT_API_H__ */

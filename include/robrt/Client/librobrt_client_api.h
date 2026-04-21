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
 * stream_stats 的 getter、内存释放、错误与版本、线程模型与所有权约定）见
 * librobrt_common.h 顶部注释。
 *
 * 调用时序：
 *   set_global_config -> init -> connect -> open_stream* -> close_stream* -> disconnect -> uninit
 *   disconnect / uninit / close_stream 均幂等强清理。
 *
 * ============================================================================
 *                    Client 端线程与调用约束（通用规则见 common.h）
 * ============================================================================
 *   - 同一 connect_cb 上的 on_state / on_notice / on_service_req 串行分发。
 *   - 同一 stream_cb 上的 on_state / on_video / on_stream_stats 串行分发；
 *     不同 stream_cb 之间可能并发。
 *   - on_video_frame 与 on_stream_stats 可能由不同线程分发（SDK 内部 RX /
 *     Worker 分离），共享业务状态需自行加锁。
 *   - set_global_config / init / uninit / connect / disconnect 必须在**同一业务
 *     线程**串行调用，且不得在任何回调内调用。
 *   - open_stream / close_stream / send_notice / reply_to_service_req /
 *     stream_get_stats / log_set_* 线程安全，可在回调内调用。
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

/*
 * 业务协议中的资源 ID（与 Service 的 stream_idx 一一对应）。
 * 使用 typedef 别名以明确语义，未来可无痛收窄/扩展。
 */
typedef int32_t robrt_stream_index_t;
typedef int32_t robrt_notice_index_t;

/******************************************************************************
 *                          ConnectInfo（设备身份 / 凭证）
 ******************************************************************************/
LIBROBRT_API_EXPORT librobrt_connect_info_t librobrt_connect_info_create(void);
LIBROBRT_API_EXPORT void                    librobrt_connect_info_destroy(librobrt_connect_info_t info);

LIBROBRT_API_EXPORT robrt_err_t librobrt_connect_info_set_device_id    (librobrt_connect_info_t info, const char *device_id);
LIBROBRT_API_EXPORT robrt_err_t librobrt_connect_info_set_device_secret(librobrt_connect_info_t info, const char *device_secret);

/*
 * Getter：写入 buf（含终止 \0）。
 *   - buf == NULL 或 buf_len == 0：仅探测，out_needed 返回所需长度（含 \0）；
 *   - buf_len 不足：返回 ROBRT_ERR_TRUNCATED，out_needed 返回所需长度；
 *   - 字段未设置：返回 ROBRT_ERR_NOT_FOUND。
 * out_needed 可为 NULL。
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_connect_info_get_device_id    (librobrt_connect_info_t info,
                                                                         char *buf, uint32_t buf_len,
                                                                         uint32_t *out_needed);
LIBROBRT_API_EXPORT robrt_err_t librobrt_connect_info_get_device_secret(librobrt_connect_info_t info,
                                                                         char *buf, uint32_t buf_len,
                                                                         uint32_t *out_needed);

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

/**
 * 通知类消息（单向，不要求回包）
 * @param payload / service_id 仅回调栈内有效；如需跨栈持有，业务层自行深拷贝。
 */
typedef void (*librobrt_on_notice_fn)(robrt_notice_index_t index,
                                      const void *payload,
                                      uint32_t len,
                                      void *userdata);

/**
 * 请求类服务消息（需要回包）
 * @param req_id      SDK 分配的请求 ID，用于 librobrt_reply_to_service_req 异步回包
 * @param service_id  业务标识；仅回调栈内有效（建议即时拷贝到业务线程）
 * @param payload     业务数据；仅回调栈内有效
 * 超时未回包时，SDK 自动以 ROBRT_ERR_TIMEOUT 应答并释放 req_id。
 * 业务可在任意线程通过 req_id 回包；req_id 仅一次有效，二次回包返回
 * ROBRT_ERR_NOT_FOUND。
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

/*
 * 获取一个由 SDK 填充了"当前版本默认值"的 stream_param，调用方可基于此对象
 * 增量 set 修改后传入 open_stream。调用方仍需 stream_param_destroy。
 *
 * 相较于"向 open_stream 传 NULL 走默认"，该 API 允许业务显式读回默认值并
 * 据此做判断；默认集合随次版本稳定（见 librobrt_open_stream 注释）。
 */
LIBROBRT_API_EXPORT librobrt_stream_param_t librobrt_stream_param_default(void);

LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_param_set_preferred_codec   (librobrt_stream_param_t p, robrt_codec_t codec);
LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_param_set_preferred_max_size(librobrt_stream_param_t p,
                                                                             uint32_t max_width,
                                                                             uint32_t max_height);
LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_param_set_preferred_fps     (librobrt_stream_param_t p, uint32_t fps);
/* 0 表示 SDK 默认超时策略 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_param_set_open_timeout_ms   (librobrt_stream_param_t p, uint32_t timeout_ms);

/*
 * Getter：读回当前设置值。统一返回 robrt_err_t，以消除"未设置"与"显式设为 0 /
 * UNKNOWN"的语义歧义：
 *   - 字段未被 setter 赋值过：返回 ROBRT_ERR_NOT_FOUND，out 不被写入；
 *   - 字段已被显式设置（包括 0）：返回 ROBRT_OK，值写入 out。
 * out 指针不得为 NULL（违反返回 ROBRT_ERR_PARAM）。
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_param_get_preferred_codec    (librobrt_stream_param_t p,
                                                                              robrt_codec_t *out_codec);
LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_param_get_preferred_max_size (librobrt_stream_param_t p,
                                                                              uint32_t *out_max_width,
                                                                              uint32_t *out_max_height);
LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_param_get_preferred_fps      (librobrt_stream_param_t p,
                                                                              uint32_t *out_fps);
LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_param_get_open_timeout_ms    (librobrt_stream_param_t p,
                                                                              uint32_t *out_timeout_ms);

/******************************************************************************
 *                          StreamCb
 ******************************************************************************/

/** 流状态变化；reason 在异常状态时为原因错误码
 *  终态：收到 CLOSED / FAILED 后 handle 失效，不得再传入任何 API。
 */
typedef void (*librobrt_on_stream_state_fn)(librobrt_stream_handle_t handle,
                                            robrt_stream_state_t state,
                                            robrt_err_t reason,
                                            void *userdata);

/** 视频帧到达；frame 仅回调栈内有效，跨栈持有用 librobrt_video_frame_retain */
typedef void (*librobrt_on_video_frame_fn)(librobrt_stream_handle_t handle,
                                           librobrt_video_frame_t frame,
                                           void *userdata);

/**
 * 流统计（SDK 周期 push）
 * @param handle  触发统计回调的流句柄，避免多路拉流时归属歧义
 * @param stats   默认仅回调栈内有效；如需跨栈/跨线程持有，使用
 *                librobrt_stream_stats_retain / librobrt_stream_stats_release
 */
typedef void (*librobrt_on_stream_stats_fn)(librobrt_stream_handle_t handle,
                                            librobrt_stream_stats_t stats,
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

/**
 * 设置全局配置。
 *   - 必须在 librobrt_init 之前调用；init 之后调用返回 ROBRT_ERR_STATE；
 *   - 可在 init 前多次调用，**后调用者覆盖前者**（last-writer-wins）；
 *   - SDK 在本函数返回前完成深拷贝；调用方可立即 destroy 传入的 cfg；
 *   - uninit 之后可再次调用 set_global_config 并重新 init，实现热重启。
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_set_global_config(librobrt_global_config_t cfg);

/**
 * 初始化（线程池 / SSL / WebRTC 栈等）。
 *   - **同步**：函数返回即表示 SDK 进入可用态；失败时返回非 OK 且内部状态保持未初始化；
 *   - 幂等：重复调用返回 ROBRT_OK，不重新初始化。
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_init(void);

/**
 * 反初始化；幂等强清理。
 *   - **同步阻塞**：内部自动 disconnect + 关闭所有 stream，**等待所有工作线程 join**、
 *     所有回调执行完毕后才返回（见 common.h "回调 drain 保证"）；
 *   - 返回后业务可安全释放所有 userdata；
 *   - 不得在任何回调内调用。
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_uninit(void);

/**
 * 连接设备到服务端；一个进程仅允许单设备连接。
 *   - **异步**：函数返回 ROBRT_OK 仅表示参数校验通过、连接流程已启动，
 *     此时状态为 CONNECTING；真正 CONNECTED 由 on_connect_state 通知；
 *   - 连接失败（鉴权、网络、License 等）通过 on_connect_state(FAILED, reason) 通知，
 *     不通过本函数返回值传递；
 *   - 同步返回失败（非 OK）仅表示**未能进入 CONNECTING**：如参数非法、
 *     SDK 未 init、已存在活动连接（ROBRT_ERR_STATE）等；
 *   - connect_info / connect_cb 在本函数返回 OK 时已被 SDK 深拷贝，调用方可立即 destroy。
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_connect(librobrt_connect_info_t connect_info,
                                                 librobrt_connect_cb_t   connect_cb);

/**
 * 断开连接；幂等强清理。
 *   - **异步**：函数返回 ROBRT_OK 表示断开流程已启动，状态进入 DISCONNECTED 过渡态；
 *     所有未 close 的 stream 被 SDK 内部自动标记为 CLOSING；
 *     最终的 on_connect_state(DISCONNECTED) 与各流 on_stream_state(CLOSED) 会被异步派发；
 *   - 同步返回后 SDK 保证**不再派发新的**属于该会话的回调（drain 语义见 common.h）；
 *   - 若需"返回即完全停止、所有回调已执行完"，请调用 uninit；
 *   - 可重复调用：已断开状态下调用返回 ROBRT_OK（no-op）。
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_disconnect(void);

/**
 * 打开订阅流（**异步**：函数 ROBRT_OK 返回后流处于 OPENING，真正 OPENED 由
 * on_stream_state 通知）
 *
 * @param index       业务协议中的资源 ID
 * @param param       订阅偏好。传 NULL 表示使用与一次 `librobrt_stream_param_create()` 后未调用任何
 *                    setter 相同的默认 hint；该默认集合在**次版本（minor）内保持稳定**，主版本
 *                    变更会写入发行说明：
 *                    - preferred_codec = ROBRT_CODEC_UNKNOWN
 *                    - max_width / max_height = 0（不限制）
 *                    - fps = 0（由对端/SDK 默认）
 *                    - open_timeout_ms = 0（SDK 内置默认超时策略）
 *                    同步返回后 SDK 已完成 param 深拷贝，调用方可立即 destroy。
 * @param cb          流回调（video/state/可选 stats）；SDK 深拷贝，可立即 destroy
 * @param out_handle  输出流句柄；ROBRT_OK 时即可用于 close_stream / get_stats。
 *                    在 OPENING 状态下调用 close_stream 合法，SDK 会中止协商并
 *                    最终推送 on_stream_state(CLOSED)。
 *
 * 失败返回：
 *   - ROBRT_ERR_STATE       当前未 CONNECTED
 *   - ROBRT_ERR_STREAM_ALREADY_OPEN   同 index 已有未关闭的流
 *   - ROBRT_ERR_BUSY        达到 ROBRT_LIMIT_MAX_STREAMS
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_open_stream(robrt_stream_index_t index,
                                                     librobrt_stream_param_t param,
                                                     librobrt_stream_cb_t    cb,
                                                     librobrt_stream_handle_t *out_handle);

/**
 * 关闭订阅流；幂等。
 * 异步：函数返回后流进入 CLOSING，on_stream_state(CLOSED) 才真正释放 handle；
 * 收到 CLOSED 之前 handle 仍可传给其它 API（将返回 ROBRT_ERR_STATE）。
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_close_stream(librobrt_stream_handle_t handle);

/**
 * 主动拉取一次流统计快照
 * @param out_stats  成功时返回一个 retained snapshot；调用方使用完成后必须
 *                   调用 librobrt_stream_stats_release
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_stream_get_stats(librobrt_stream_handle_t handle,
                                                          librobrt_stream_stats_t *out_stats);

/******************************************************************************
 *                          信令消息收发
 ******************************************************************************/

/*
 * v1 能力边界：
 * - 支持 Client -> Service 单向通知：librobrt_send_notice
 * - 支持 Service -> Client 请求，Client 以 librobrt_reply_to_service_req 异步应答
 * - v1 暂不提供 Client 主动 request Service 并等待 response 的公开 API
 */

/**
 * 主动向 Service 发送业务通知（fire-and-forget）。
 * payload 由 SDK 在函数返回前完成拷贝，调用方可立即释放 buffer。
 * 未 CONNECTED 时返回 ROBRT_ERR_STATE；len 超过 ROBRT_LIMIT_MAX_PAYLOAD_BYTES
 * 返回 ROBRT_ERR_PARAM。
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_send_notice(robrt_notice_index_t index,
                                                     const void *payload,
                                                     uint32_t len);

/**
 * 对 `on_service_req` 收到的请求异步回包（Client → Service）。
 * 与 Service 侧的 `librobrt_svc_service_reply` 不同：本函数仅在 **Client 库**中，
 * 用于应答对端发来的请求。
 *
 * 所有权：payload 由 SDK 在函数返回前完成拷贝，调用方可立即释放 buffer。
 * 幂等：req_id 仅一次有效；二次调用返回 ROBRT_ERR_NOT_FOUND（已超时或已回包）。
 * 线程：可在任意线程调用，包括 on_service_req 回调内。
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_reply_to_service_req(uint64_t req_id,
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

/**
 * @file     librobrt_common.h
 * @brief    Client / Service 两端共享的基础类型、枚举、共享 opaque 对象与 API
 *
 * Client 头（Client/librobrt_client_api.h）与 Service 头（Service/librobrt_service_api.h）
 * 均 include 本文件。共享内容只声明一次，降低维护成本，同时保证两端 ABI 对齐。
 *
 * 约束：
 *   - 所有对外类型保持 opaque（typedef 指向未定义的 struct *）；
 *   - 枚举值只追加、不修改、不复用；
 *   - 函数签名只增不改；
 *   - 共享函数（如 log_config_*、video_frame_*、string_free 等）在 Client 和 Service 库中
 *     都各自实现/导出一份，以保证两端独立部署时的自包含性。
 *
 * ============================================================================
 *                    线程模型（Threading Model）— 两端通用
 * ============================================================================
 *   - 回调统一在 SDK 内部工作线程触发，**不在业务调用线程**。
 *   - 同一 opaque 对象（connect_cb / stream_cb 等）上的全部回调在 SDK 内部
 *     保证串行分发；不同对象之间可能并发。
 *   - 回调内禁止：阻塞 I/O、长耗时业务、调用 init/uninit/connect/disconnect。
 *   - 回调内允许（白名单）：
 *       * 所有 getter（video_frame_get_* / stream_stats_get_* / ...）
 *       * retain / release
 *       * close_stream / destroy_stream
 *       * send_notice / reply_to_service_req / svc_service_reply / post_topic
 *       * log_set_level / log_set_callback
 *   - `userdata` 在对应 cb 对象 destroy 前保持有效；SDK 不接管其生命周期。
 *   - 回调 drain 保证：
 *       * `disconnect` 同步返回后，保证不再有属于该 connect 会话的**新**回调
 *         被派发；但已经进入 dispatch 队列的回调仍会执行，直至 uninit。
 *       * `uninit` 同步返回前，SDK **保证**内部工作线程已全部 join、所有回调
 *         已执行完毕（happens-before uninit 返回）。业务可在 uninit 之后安全
 *         释放所有 userdata 以及与回调共享的资源，不会产生 UAF。
 *       * `close_stream` / `destroy_stream` 返回后，属于该 stream 的新回调
 *         不再触发；已入队回调会在 uninit 前执行完毕。
 *
 * ============================================================================
 *                    所有权与生命周期（Ownership & Lifetime）— 两端通用
 * ============================================================================
 *   - xxx_create / xxx_destroy 严格配对。
 *   - 传入 SDK 的 opaque 配置对象（connect_info / connect_cb / stream_param /
 *     stream_cb 等）在对应 API（connect / open_stream / create_stream）返回
 *     ROBRT_OK 的瞬间，SDK 已完成内部拷贝，调用方可立即 destroy。
 *   - 传入 SDK 的 `const void *payload` / `const char *` 在 API 同步返回前由
 *     SDK 完成拷贝；API 返回后调用方可释放原 buffer（异步回包类 API 同理）。
 *   - SDK 通过 out 参数返回的 `char *` / `void *` 必须用 librobrt_string_free /
 *     librobrt_buffer_free 释放。
 *   - 回调入参的只读 opaque 句柄（video_frame_t / stream_stats_t）默认仅回调
 *     栈内有效；需跨栈/跨线程持有必须 retain，使用完 release。
 *
 * ============================================================================
 *                    容量与上限（Limits & Quotas）— 默认值
 * ============================================================================
 *   - 一个进程只能运行一份 Client 库（连接单一设备到服务端）。
 *   - 同一 connect 会话下同时打开的 stream 数量：≤ ROBRT_LIMIT_MAX_STREAMS。
 *   - 单条 notice / service_req payload：≤ ROBRT_LIMIT_MAX_PAYLOAD_BYTES。
 *   - 具体数值见下方 ROBRT_LIMIT_* 宏；次版本内稳定，主版本变更会写入发行说明。
**/

#ifndef __LIBROBRT_COMMON_H__
#define __LIBROBRT_COMMON_H__

#include "librobrt_version.h"
#include "librobrt_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *                                 错误码
 ******************************************************************************/
typedef int32_t robrt_err_t;

#define ROBRT_OK                              0

/* 通用错误 0x0xxx */
#define ROBRT_ERR_FAIL                        (-0x0001)  /* 未知失败 */
#define ROBRT_ERR_PARAM                       (-0x0002)  /* 非法参数 */
#define ROBRT_ERR_NOT_SUPPORT                 (-0x0003)  /* 不支持 */
#define ROBRT_ERR_STATE                       (-0x0004)  /* 当前状态不允许该操作 */
#define ROBRT_ERR_NO_MEM                      (-0x0005)  /* 内存分配失败 */
#define ROBRT_ERR_TIMEOUT                     (-0x0006)  /* 超时 */
#define ROBRT_ERR_BUSY                        (-0x0007)  /* 资源繁忙 */
#define ROBRT_ERR_NOT_FOUND                   (-0x0008)  /* 对象不存在 */
#define ROBRT_ERR_TRUNCATED                   (-0x0009)  /* 输出缓冲区过小 */

/* 连接错误 0x1xxx */
#define ROBRT_ERR_CONN_FAIL                   (-0x1001)
#define ROBRT_ERR_CONN_AUTH                   (-0x1002)
#define ROBRT_ERR_CONN_LICENSE                (-0x1003)
#define ROBRT_ERR_CONN_NETWORK                (-0x1004)
#define ROBRT_ERR_CONN_KICKED                 (-0x1005)  /* 被服务端踢出 */

/* 流错误 0x2xxx */
#define ROBRT_ERR_STREAM_NOT_EXIST            (-0x2001)
#define ROBRT_ERR_STREAM_ALREADY_OPEN         (-0x2002)
#define ROBRT_ERR_STREAM_CODEC_UNSUPP         (-0x2003)
#define ROBRT_ERR_STREAM_ENCODER_FAIL         (-0x2004)
#define ROBRT_ERR_STREAM_NO_SUBSCRIBER        (-0x2005)

/******************************************************************************
 *                                 容量常量
 * 次版本（minor）内稳定；主版本变更会写入发行说明。
 ******************************************************************************/
#define ROBRT_LIMIT_MAX_STREAMS               16          /* 单进程并发流上限 */
#define ROBRT_LIMIT_MAX_PAYLOAD_BYTES         (1u << 20)  /* 单条信令 payload 上限 1 MiB */
#define ROBRT_LIMIT_MAX_URL_LEN               1024
#define ROBRT_LIMIT_MAX_DEVICE_ID_LEN         128
#define ROBRT_LIMIT_MAX_SERVICE_ID_LEN        64

/******************************************************************************
 *                                 枚举
 ******************************************************************************/

/* 日志等级 */
typedef enum {
    ROBRT_LOG_TRACE = 0,
    ROBRT_LOG_DEBUG = 1,
    ROBRT_LOG_INFO  = 2,
    ROBRT_LOG_WARN  = 3,
    ROBRT_LOG_ERROR = 4,
    ROBRT_LOG_FATAL = 5,
} robrt_log_level_t;

/* 视频编码 / 像素格式 */
typedef enum {
    ROBRT_CODEC_UNKNOWN = 0,

    /* 原始 YUV 族 */
    ROBRT_CODEC_I420    = 0x0001,
    ROBRT_CODEC_NV12    = 0x0002,
    ROBRT_CODEC_YUYV    = 0x0003,
    ROBRT_CODEC_UYVY    = 0x0004,

    /* 压缩族 */
    ROBRT_CODEC_H264    = 0x1001,
    ROBRT_CODEC_H265    = 0x1002,
    ROBRT_CODEC_MJPEG   = 0x1003,
} robrt_codec_t;

/* 视频帧类型 */
typedef enum {
    ROBRT_FRAME_UNKNOWN = 0,
    ROBRT_FRAME_I       = 1,
    ROBRT_FRAME_P       = 2,
    ROBRT_FRAME_B       = 3,
} robrt_frame_type_t;

/* 连接状态 */
typedef enum {
    ROBRT_CONN_IDLE         = 0,
    ROBRT_CONN_CONNECTING   = 1,
    ROBRT_CONN_CONNECTED    = 2,
    ROBRT_CONN_DISCONNECTED = 3,
    ROBRT_CONN_FAILED       = 4,
} robrt_connect_state_t;

/*
 * 流状态（Client / Service 共用，同名同义）：
 *   - IDLE
 *       流对象已存在，但当前未进行媒体收发。
 *       Service: create_stream 成功后、stop_stream 完成后处于此状态，可再次 start_stream。
 *       Client: 枚举保留为共享语义；公开拉流句柄通常不会长期停留在该状态。
 *
 *   - OPENING
 *       异步建流 / 启动中。句柄有效，可接受 close_stream / destroy_stream 取消流程。
 *
 *   - OPENED
 *       流已就绪并处于正常媒体收发状态。
 *
 *   - CLOSING
 *       异步关闭中。该状态只表示关闭流程已开始；之后只允许落到 CLOSED。
 *
 *   - CLOSED
 *       正常关闭终态。由 close_stream / destroy_stream / disconnect / uninit 等主动或受控清理触发。
 *       这是终态；SDK 不再为该 handle 派发新的 stream 回调。
 *
 *   - FAILED
 *       异常失败终态。由协商失败、链路故障、编解码异常等不可恢复错误触发。
 *       这是终态；SDK 不再为该 handle 派发新的 stream 回调。
 *
 * 统一约束：
 *   - CLOSED 与 FAILED 都是终态，语义不同但都表示该 handle 生命周期结束；
 *   - 不存在 FAILED -> CLOSED 的二次终态通知；调用方只会收到一个终态；
 *   - reason 仅在 FAILED 时表示失败原因；其余状态统一为 ROBRT_OK。
 *
 * 采集路径切换语义（同一逻辑流内）：
 *   - “切采集路径 / 切相机 / 切输入源”属于控制面动作，不属于流生命周期动作；
 *   - 若切换在同一逻辑 stream 内完成且媒体链路可持续复用，则不触发 stream_state
 *     跳变：不派发 OPENING / CLOSING / CLOSED，仅表现为媒体内容切换，handle 与 index
 *     保持不变；
 *   - 切换成功/失败的业务确认应通过控制面消息完成（notice / request-reply / topic），
 *     而不是复用 stream_state；
 *   - 只有当切换导致该逻辑流无法继续工作，且 SDK 无法在当前 handle 内恢复时，
 *     才派发 stream_state(FAILED, reason)。
 *
 * 预热缓冲语义（Service 内部实现约束）：
 *   - 预热缓冲仅适用于 Service 侧 stream 处于 OPENING 的窗口，用于隐藏 WebRTC
 *     建链、编码器预热、关键帧准备等启动时延；
 *   - 预热缓冲是每路 stream 的 SDK 内部有界实时队列，对调用方不可见，不引入额外状态；
 *   - 流从 OPENING 进入 OPENED 后，SDK 按实时策略发送预热缓冲中的可用帧；若部分帧因
 *     时效性被丢弃，仍不视为接口错误；
 *   - 流进入 IDLE / CLOSING / CLOSED / FAILED，或发生 stop / destroy / disconnect /
 *     uninit 时，未消费的预热缓冲全部丢弃；
 *   - Client 侧不存在对业务可见的“预热缓冲状态”；Client 收到 OPENED 仅表示接收链路
 *     已就绪，不保证首帧已到达。
 */
typedef enum {
    ROBRT_STREAM_IDLE     = 0,
    ROBRT_STREAM_OPENING  = 1,
    ROBRT_STREAM_OPENED   = 2,
    ROBRT_STREAM_CLOSING  = 3,
    ROBRT_STREAM_CLOSED   = 4,
    ROBRT_STREAM_FAILED   = 5,
} robrt_stream_state_t;

/* 信令服务器工作模式（Service 端常用；Client 端一般走默认 SIGNAL 模式） */
typedef enum {
    ROBRT_SIGNAL_MODE_SERVER  = 1,   /* 依赖外部信令服务器 */
    ROBRT_SIGNAL_MODE_DIRECT  = 2,   /* 设备端自己充当信令服务器 */
} robrt_signal_mode_t;

/* 码率控制类型（Service 端编码器参数） */
typedef enum {
    ROBRT_RC_CQP  = 0,
    ROBRT_RC_CBR  = 1,
    ROBRT_RC_VBR  = 2,
    ROBRT_RC_ACQP = 3,  /* CQP 基础上开启峰控 */
} robrt_rc_mode_t;

/* 地区 */
typedef enum {
    ROBRT_REGION_CN       = 0,
    ROBRT_REGION_OVERSEAS = 1,
} robrt_region_t;

/******************************************************************************
 *                        Opaque Handles — 共享配置 / 数据对象
 ******************************************************************************/

/* 共享配置：两端使用相同 ABI */
typedef struct librobrt_log_config_s*      librobrt_log_config_t;
typedef struct librobrt_signal_config_s*   librobrt_signal_config_t;
typedef struct librobrt_license_config_s*  librobrt_license_config_t;
typedef struct librobrt_global_config_s*   librobrt_global_config_t;

/* 共享只读数据对象（回调入参） */
typedef const struct librobrt_video_frame_s*   librobrt_video_frame_t;
typedef const struct librobrt_stream_stats_s*  librobrt_stream_stats_t;

/******************************************************************************
 *                        业务协议 ID 别名（Client / Service 共享）
 ******************************************************************************/
/*
 * 业务协议中的资源 ID：Client 侧 open_stream 与 Service 侧 create_stream 使用
 * 同一 index 空间，需一一对应。使用 typedef 别名以明确语义，未来可无痛收窄/扩展。
 * 约束：
 *   - 建议业务使用非负值；负值保留给 SDK 内部诊断，不保证未来不赋予特殊语义。
 *   - index 的具体取值范围由业务协议自定义，SDK 不校验上限。
 *   - 当前 v1 约定：仅支持主路 stream_idx = 0；若未来扩多路，再在保持兼容的前提下
 *     追加 1, 2, ... 的业务语义。
 */
typedef int32_t robrt_stream_index_t;
typedef int32_t robrt_notice_index_t;

/******************************************************************************
 *                           通用工具 API
 ******************************************************************************/

/* 内存释放：所有 SDK 返回的 char* / void* 必须调用对应 free */
LIBROBRT_API_EXPORT void  librobrt_string_free(char *p);
LIBROBRT_API_EXPORT void  librobrt_buffer_free(void *p);

/**
 * 错误 / 版本 / 构建信息。
 *
 * librobrt_get_last_error():
 *   - thread-local；仅对**紧邻的上一次同步 API 调用**（在当前线程内返回非 OK 的）
 *     有意义，返回该调用附加的人类可读诊断字符串。
 *   - 不覆盖异步失败：通过 on_state / on_stream_state 的 `reason` 参数传递的
 *     错误**不会**写入 last_error，业务不应在回调内依赖此函数诊断异步原因。
 *   - 在未产生错误的线程调用返回 ""（空串）或上一次陈旧值，不可作为错误发生的
 *     判定依据。永远以 API 返回的 robrt_err_t 为准。
 *   - 返回字符串生命周期持续到当前线程下一次进入任意 librobrt_* API 为止。
 */
LIBROBRT_API_EXPORT const char* librobrt_get_last_error(void);
LIBROBRT_API_EXPORT void        librobrt_get_version(uint32_t *major, uint32_t *minor, uint32_t *patch);
LIBROBRT_API_EXPORT const char* librobrt_get_build_info(void);

/**
 * 错误码 → 静态字符串名（如 "ROBRT_ERR_PARAM"），用于日志。
 * 返回值为静态字符串，调用方不得 free。未知错误码返回 "ROBRT_ERR_UNKNOWN"。
 */
LIBROBRT_API_EXPORT const char* librobrt_err_to_string(robrt_err_t err);

/******************************************************************************
 *                           LogConfig（共享）
 ******************************************************************************/
typedef void (*librobrt_log_cb_fn)(robrt_log_level_t level,
                                   const char *msg,
                                   void *userdata);

LIBROBRT_API_EXPORT librobrt_log_config_t librobrt_log_config_create(void);
LIBROBRT_API_EXPORT void                  librobrt_log_config_destroy(librobrt_log_config_t cfg);

LIBROBRT_API_EXPORT robrt_err_t librobrt_log_config_set_level   (librobrt_log_config_t cfg, robrt_log_level_t level);
LIBROBRT_API_EXPORT robrt_err_t librobrt_log_config_set_enable  (librobrt_log_config_t cfg, bool enable);
LIBROBRT_API_EXPORT robrt_err_t librobrt_log_config_set_callback(librobrt_log_config_t cfg,
                                                                 librobrt_log_cb_fn cb,
                                                                 void *userdata);
LIBROBRT_API_EXPORT robrt_log_level_t librobrt_log_config_get_level (librobrt_log_config_t cfg);
LIBROBRT_API_EXPORT bool              librobrt_log_config_get_enable(librobrt_log_config_t cfg);

/******************************************************************************
 *                           SignalConfig（共享）
 ******************************************************************************/
LIBROBRT_API_EXPORT librobrt_signal_config_t librobrt_signal_config_create(void);
LIBROBRT_API_EXPORT void                     librobrt_signal_config_destroy(librobrt_signal_config_t cfg);

LIBROBRT_API_EXPORT robrt_err_t librobrt_signal_config_set_url        (librobrt_signal_config_t cfg, const char *url);
/* 仅 Service 端可能用到；Client 端默认 SERVER 模式，不需调用 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_signal_config_set_mode       (librobrt_signal_config_t cfg, robrt_signal_mode_t mode);
LIBROBRT_API_EXPORT robrt_err_t librobrt_signal_config_set_direct_port(librobrt_signal_config_t cfg, uint16_t port);
LIBROBRT_API_EXPORT robrt_err_t librobrt_signal_config_set_connect_timeout_ms(librobrt_signal_config_t cfg,
                                                                               uint32_t timeout_ms);
LIBROBRT_API_EXPORT robrt_err_t librobrt_signal_config_set_keepalive_interval_ms(librobrt_signal_config_t cfg,
                                                                                  uint32_t interval_ms);
LIBROBRT_API_EXPORT robrt_err_t librobrt_signal_config_set_reconnect_enable(librobrt_signal_config_t cfg,
                                                                             bool enable);
LIBROBRT_API_EXPORT robrt_err_t librobrt_signal_config_set_reconnect_backoff_ms(librobrt_signal_config_t cfg,
                                                                                 uint32_t initial_backoff_ms,
                                                                                 uint32_t max_backoff_ms);
/* 0 表示不限重试次数 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_signal_config_set_reconnect_max_attempts(librobrt_signal_config_t cfg,
                                                                                   uint32_t max_attempts);

/*
 * Getter：写入 buf（含终止 \0）。
 *   - buf == NULL 或 buf_len == 0：仅探测，out_needed 返回所需长度（含 \0）；
 *   - buf_len 不足：返回 ROBRT_ERR_TRUNCATED，out_needed 返回所需长度；
 *   - 字段未设置：返回 ROBRT_ERR_NOT_FOUND。
 * out_needed 可为 NULL。
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_signal_config_get_url(librobrt_signal_config_t cfg,
                                                               char *buf, uint32_t buf_len,
                                                               uint32_t *out_needed);

/******************************************************************************
 *                           LicenseConfig（共享）
 ******************************************************************************/
LIBROBRT_API_EXPORT librobrt_license_config_t librobrt_license_config_create(void);
LIBROBRT_API_EXPORT void                      librobrt_license_config_destroy(librobrt_license_config_t cfg);

LIBROBRT_API_EXPORT robrt_err_t librobrt_license_config_set_file  (librobrt_license_config_t cfg, const char *path);
LIBROBRT_API_EXPORT robrt_err_t librobrt_license_config_set_buffer(librobrt_license_config_t cfg,
                                                                   const void *data, uint32_t len);

/*
 * 读回当前配置的 license 文件路径；若通过 set_buffer 注入则返回 ROBRT_ERR_NOT_FOUND。
 * 探测语义与 librobrt_connect_info_get_device_id 一致（见该函数注释）。
 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_license_config_get_file(librobrt_license_config_t cfg,
                                                                  char *buf, uint32_t buf_len,
                                                                  uint32_t *out_needed);

/******************************************************************************
 *                           GlobalConfig（共享）
 ******************************************************************************/
LIBROBRT_API_EXPORT librobrt_global_config_t librobrt_global_config_create(void);
LIBROBRT_API_EXPORT void                     librobrt_global_config_destroy(librobrt_global_config_t cfg);

LIBROBRT_API_EXPORT robrt_err_t librobrt_global_config_set_log        (librobrt_global_config_t cfg, librobrt_log_config_t log);
LIBROBRT_API_EXPORT robrt_err_t librobrt_global_config_set_signal     (librobrt_global_config_t cfg, librobrt_signal_config_t sig);
LIBROBRT_API_EXPORT robrt_err_t librobrt_global_config_set_license    (librobrt_global_config_t cfg, librobrt_license_config_t lic);
/* 可读写的配置文件存储目录；NULL 使用默认路径 */
LIBROBRT_API_EXPORT robrt_err_t librobrt_global_config_set_config_path(librobrt_global_config_t cfg, const char *path);
LIBROBRT_API_EXPORT robrt_err_t librobrt_global_config_set_region     (librobrt_global_config_t cfg, robrt_region_t region);

/******************************************************************************
 *                           Video Frame Getter / 生命周期（共享）
 ******************************************************************************/
LIBROBRT_API_EXPORT robrt_codec_t      librobrt_video_frame_get_codec    (librobrt_video_frame_t f);
LIBROBRT_API_EXPORT robrt_frame_type_t librobrt_video_frame_get_type     (librobrt_video_frame_t f);
LIBROBRT_API_EXPORT const uint8_t*     librobrt_video_frame_get_data     (librobrt_video_frame_t f);
LIBROBRT_API_EXPORT uint32_t           librobrt_video_frame_get_data_size(librobrt_video_frame_t f);
LIBROBRT_API_EXPORT uint32_t           librobrt_video_frame_get_width    (librobrt_video_frame_t f);
LIBROBRT_API_EXPORT uint32_t           librobrt_video_frame_get_height   (librobrt_video_frame_t f);
LIBROBRT_API_EXPORT uint64_t           librobrt_video_frame_get_pts_ms   (librobrt_video_frame_t f);
LIBROBRT_API_EXPORT uint64_t           librobrt_video_frame_get_utc_ms   (librobrt_video_frame_t f);
LIBROBRT_API_EXPORT uint32_t           librobrt_video_frame_get_seq      (librobrt_video_frame_t f);
LIBROBRT_API_EXPORT int32_t            librobrt_video_frame_get_index    (librobrt_video_frame_t f);

LIBROBRT_API_EXPORT librobrt_video_frame_t librobrt_video_frame_retain (librobrt_video_frame_t f);
LIBROBRT_API_EXPORT void                   librobrt_video_frame_release(librobrt_video_frame_t f);

/******************************************************************************
 *                           Stream Stats Getter（共享）
 ******************************************************************************/
/*
 * stream_stats 默认仅在当前回调栈或当前 API 调用返回前有效。
 * 如需跨栈/跨线程持有，必须 retain；使用完成后 release。
 */
LIBROBRT_API_EXPORT librobrt_stream_stats_t librobrt_stream_stats_retain (librobrt_stream_stats_t s);
LIBROBRT_API_EXPORT void                    librobrt_stream_stats_release(librobrt_stream_stats_t s);

LIBROBRT_API_EXPORT uint32_t librobrt_stream_stats_get_duration_ms    (librobrt_stream_stats_t s);
LIBROBRT_API_EXPORT uint64_t librobrt_stream_stats_get_in_bound_bytes (librobrt_stream_stats_t s);
LIBROBRT_API_EXPORT uint64_t librobrt_stream_stats_get_in_bound_pkts  (librobrt_stream_stats_t s);
LIBROBRT_API_EXPORT uint64_t librobrt_stream_stats_get_out_bound_bytes(librobrt_stream_stats_t s);
LIBROBRT_API_EXPORT uint64_t librobrt_stream_stats_get_out_bound_pkts (librobrt_stream_stats_t s);
LIBROBRT_API_EXPORT uint32_t librobrt_stream_stats_get_lost_pkts      (librobrt_stream_stats_t s);
LIBROBRT_API_EXPORT uint32_t librobrt_stream_stats_get_bitrate_kbps   (librobrt_stream_stats_t s);
LIBROBRT_API_EXPORT uint32_t librobrt_stream_stats_get_rtt_ms         (librobrt_stream_stats_t s);
LIBROBRT_API_EXPORT uint32_t librobrt_stream_stats_get_fps            (librobrt_stream_stats_t s);

/* QoS 补充指标（Client 拉流更关心） */
LIBROBRT_API_EXPORT uint32_t librobrt_stream_stats_get_jitter_ms          (librobrt_stream_stats_t s);
LIBROBRT_API_EXPORT uint32_t librobrt_stream_stats_get_freeze_count       (librobrt_stream_stats_t s);
LIBROBRT_API_EXPORT uint32_t librobrt_stream_stats_get_decode_fail_count  (librobrt_stream_stats_t s);

#ifdef __cplusplus
}
#endif

#endif /* __LIBROBRT_COMMON_H__ */

/**
 * @file   common/public/log_tagged.h
 * @brief  带 tag 前缀的日志宏，统一 service/client 内部状态/错误日志的格式与通道。
 *
 * 为什么独立一份头：
 *   - logger_api.h 提供 RFLOW_LOG{I,W,E,...}，是 fmt-style 入口；
 *   - 各模块（service impl、core/platform、RTC 路径等）需要带模块名 tag（如 "[PushStreamer]"）。
 *   - 本宏把 tag 前缀拼进 fmt 后交给 RFLOW_LOG*，与 librflow_log_set_callback / 日志级别一致。
 *
 * 使用约定：
 *   - SDK 库内业务与底层（含 Rockchip MPP、调试 trace）优先使用 RFLOW_LOG_TAG_*，避免直写 std::cout。
 *   - 独立进程（如 apps/signaling_server）可选用 RFLOW_LOG_* 与 SDK 同一回调/级别；未注册回调时默认 stderr。其它 demo/示例仍可直写控制台做本地调试。
 *   - 超大 SDP 文本等调试 dump：仍可用 RFLOW_LOG_TAG_I/D 一次打出（或通过 knob 限制频率）。
 */

#ifndef __RFLOW_COMMON_PUBLIC_LOG_TAGGED_H__
#define __RFLOW_COMMON_PUBLIC_LOG_TAGGED_H__

#include "public/logger_api.h"

#define RFLOW_LOG_TAG_T(tag, fmt, ...) RFLOW_LOGT("[" tag "] " fmt, ##__VA_ARGS__)
#define RFLOW_LOG_TAG_D(tag, fmt, ...) RFLOW_LOGD("[" tag "] " fmt, ##__VA_ARGS__)
#define RFLOW_LOG_TAG_I(tag, fmt, ...) RFLOW_LOGI("[" tag "] " fmt, ##__VA_ARGS__)
#define RFLOW_LOG_TAG_W(tag, fmt, ...) RFLOW_LOGW("[" tag "] " fmt, ##__VA_ARGS__)
#define RFLOW_LOG_TAG_E(tag, fmt, ...) RFLOW_LOGE("[" tag "] " fmt, ##__VA_ARGS__)

#endif  // __RFLOW_COMMON_PUBLIC_LOG_TAGGED_H__

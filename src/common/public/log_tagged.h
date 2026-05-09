/**
 * @file   common/public/log_tagged.h
 * @brief  带 tag 前缀的日志宏，统一 service/client 内部状态/错误日志的格式与通道。
 *
 * 为什么独立一份头：
 *   - logger_api.h 提供 RFLOW_LOG{I,W,E,...}，是 fmt-style 入口；
 *   - 业务 impl（PushStreamer / PullSubscriber 等）需要带模块名 tag："[PushStreamer] ..."；
 *   - 历史代码大量直接走 std::cout << "[Tag] ..."，业务无法通过 librflow_log_set_callback
 *     拦截。本宏把 tag 前缀直接拼进 fmt，再交给 RFLOW_LOG*，彻底走 SDK 日志通道。
 *
 * 与 std::cout 的分工：
 *   - 状态切换 / 错误 / 重要 OK 路径：必须用本宏（业务可拦截、可分级）。
 *   - 多行 SDP dump / 二进制 / 高频 trace（仅 knob 打开时）：保留 std::cout/std::cerr，
 *     因为它们体量大、跨行、调试通道输出更直观；业务侧本就不会订阅。
 */

#ifndef __RFLOW_COMMON_PUBLIC_LOG_TAGGED_H__
#define __RFLOW_COMMON_PUBLIC_LOG_TAGGED_H__

#include "common/public/logger_api.h"

#define RFLOW_LOG_TAG_T(tag, fmt, ...) RFLOW_LOGT("[" tag "] " fmt, ##__VA_ARGS__)
#define RFLOW_LOG_TAG_D(tag, fmt, ...) RFLOW_LOGD("[" tag "] " fmt, ##__VA_ARGS__)
#define RFLOW_LOG_TAG_I(tag, fmt, ...) RFLOW_LOGI("[" tag "] " fmt, ##__VA_ARGS__)
#define RFLOW_LOG_TAG_W(tag, fmt, ...) RFLOW_LOGW("[" tag "] " fmt, ##__VA_ARGS__)
#define RFLOW_LOG_TAG_E(tag, fmt, ...) RFLOW_LOGE("[" tag "] " fmt, ##__VA_ARGS__)

#endif  // __RFLOW_COMMON_PUBLIC_LOG_TAGGED_H__

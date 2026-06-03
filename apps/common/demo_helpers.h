#ifndef __RFLOW_APPS_COMMON_DEMO_HELPERS_H__
#define __RFLOW_APPS_COMMON_DEMO_HELPERS_H__

// apps/common — 推/拉 demo 之间共享的少量轻量 helper：
//   - 进程级停止 flag + SIGINT/SIGTERM 处理；忽略 SIGHUP/SIGPIPE 以免 SSH 断线或终端关闭误杀 demo；
//   - connect/stream 状态打印；
//   - Linux 下相机路径解析（与 RFLOW_PUSH_DEMO_CAMERA 环境变量约定）。
//
// 只面向 demo，不进 SDK；故仅依赖 libc / C++ 标准库 + libRoboFlow 公共 ABI。

#include "rflow/librflow_common.h"

#include <atomic>
#include <string>

namespace rflow::apps::common {

// 注册 SIGINT/SIGTERM，将进程级 stop flag 置位。
void InstallStopSignals();

// 是否已请求停止（由信号处理写入）。
bool StopRequested();

// 打印 OnConnectState 回调常用信息（stdout）。
void LogConnectState(rflow_connect_state_t state, rflow_err_t reason);

// 打印 OnStreamState 回调常用信息（stdout）。
void LogStreamState(rflow_stream_state_t state, rflow_err_t reason);

// Linux 下：若 cli 已传入 camera 则直接返回；否则读 RFLOW_PUSH_DEMO_CAMERA，
// 缺省回退到 /dev/video0。其他平台返回空串。
std::string PickLinuxCameraPath(std::string cli_value);

}  // namespace rflow::apps::common

#endif  // __RFLOW_APPS_COMMON_DEMO_HELPERS_H__

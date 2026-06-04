#ifndef __RFLOW_APPS_COMMON_DEMO_HELPERS_H__
#define __RFLOW_APPS_COMMON_DEMO_HELPERS_H__

// apps/common — demo 私有 helper（不进 SDK）：
//   demo_helpers.*          推/拉共用：信号、状态日志、相机路径解析
//   demo_camera_hotplug.*   push 专用：Linux USB 相机 udev/探测
//   demo_push_session.*     push 专用：信令 + 推流会话与热插拔调度
//
// 只面向 demo，不进 SDK；故仅依赖 libc / C++ 标准库 + libRoboFlow 公共 ABI。

#include "rflow/librflow_common.h"

#include <atomic>
#include <string>

namespace rflow::apps::common {

struct PushDemoConfig;

// 注册 SIGINT/SIGTERM（sigaction），将进程级 stop flag 置位。
void InstallStopSignals();

// 是否已请求停止（由信号处理或失败阈值写入）。
bool StopRequested();

// 请求主循环退出（如 RFLOW_PUSH_DEMO_MAX_FAILURES 触发）。
void RequestDemoStop();

// 打印 OnConnectState 回调常用信息（stdout）。
void LogConnectState(rflow_connect_state_t state, rflow_err_t reason);

// 打印 OnStreamState 回调常用信息（stdout）。
void LogStreamState(rflow_stream_state_t state, rflow_err_t reason);

// Linux 下：若 cli 已传入 camera 则直接返回；否则读 RFLOW_PUSH_DEMO_CAMERA，
// 再否则 udev capture 探测（push 构建），最后回退 /dev/video0。
std::string PickLinuxCameraPath(std::string cli_value);

void PrintPushDemoUsage(const char* argv0);

// 解析 push_demo_sdk 命令行；失败时打印用法并返回 false。
bool ParsePushDemoConfig(int argc, char** argv, struct PushDemoConfig& cfg);

#if defined(__linux__) && defined(RFLOW_APPS_PUSH_DEMO)
// 显式指定相机路径但尚不可 open 时打印警告（主循环仍会重试）。
void WarnIfCameraPathNotReady(const std::string& path);
#endif

}  // namespace rflow::apps::common

#endif  // __RFLOW_APPS_COMMON_DEMO_HELPERS_H__

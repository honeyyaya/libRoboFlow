#include "common/demo_helpers.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>

namespace rflow::apps::common {

namespace {

std::atomic<bool>& StopFlag() {
    static std::atomic<bool> flag{false};
    return flag;
}

void OnSig(int /*signo*/) {
    StopFlag().store(true, std::memory_order_release);
}

}  // namespace

void InstallStopSignals() {
    // SSH 会话断开、本地熄屏休眠导致断网时，shell 会对前台进程发 SIGHUP；默认会退出。
    // demo 在板子上常经 SSH 启动，忽略 SIGHUP/SIGPIPE 后进程可继续推流（仍可用 Ctrl+C / SIGTERM 停止）。
    std::signal(SIGHUP, SIG_IGN);
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, OnSig);
    std::signal(SIGTERM, OnSig);
}

bool StopRequested() {
    return StopFlag().load(std::memory_order_acquire);
}

void LogConnectState(rflow_connect_state_t state, rflow_err_t reason) {
    std::cout << "[demo] connect state=" << state << " reason=" << reason << std::endl;
}

void LogStreamState(rflow_stream_state_t state, rflow_err_t reason) {
    std::cout << "[demo] stream state=" << state << " reason=" << reason << std::endl;
}

std::string PickLinuxCameraPath(std::string cli_value) {
#if defined(__linux__)
    if (!cli_value.empty()) return cli_value;
    if (const char* env_camera = std::getenv("RFLOW_PUSH_DEMO_CAMERA")) {
        if (env_camera[0] != '\0') return std::string(env_camera);
    }
    return std::string("/dev/video0");
#else
    (void)cli_value;
    return std::string();
#endif
}

}  // namespace rflow::apps::common

#include "common/demo_helpers.h"

#include "common/demo_push_session.h"

#include <atomic>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <iostream>

#if defined(__linux__)
#  include <cstring>
#endif

#if defined(__linux__) && defined(RFLOW_APPS_PUSH_DEMO)
#  include "common/demo_v4l_linux.h"
#endif

namespace rflow::apps::common {

namespace {

std::atomic<bool>& StopFlag() {
    static std::atomic<bool> flag{false};
    return flag;
}

void OnSig(int /*signo*/) {
    StopFlag().store(true, std::memory_order_release);
}

bool IsPositiveIntArg(const char* s) {
    if (!s || !s[0]) return false;
    for (const char* p = s; *p; ++p) {
        if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
    }
    return true;
}

}  // namespace

void InstallStopSignals() {
    struct sigaction sa {};
    sa.sa_handler = OnSig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags   = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

void RequestDemoStop() {
    StopFlag().store(true, std::memory_order_release);
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
#  if defined(RFLOW_APPS_PUSH_DEMO)
    return ProbeDefaultLinuxCaptureDevice();
#  else
    return std::string("/dev/video0");
#  endif
#else
    (void)cli_value;
    return std::string();
#endif
}

void PrintPushDemoUsage(const char* argv0) {
    const char* prog = (argv0 && argv0[0]) ? argv0 : "push_demo_sdk";
    std::cerr
        << "Usage: " << prog
        << " <signaling_url> [device_id] [width] [height] [fps] [stream_idx] [camera]\n"
        << "  defaults: device_id=demo_device width=1280 height=720 fps=60 stream_idx=0\n"
        << "  Linux camera: argv[7] > RFLOW_PUSH_DEMO_CAMERA > first udev capture /dev/videoN\n"
        << "  env: RFLOW_PUSH_DEMO_CAMERA, RFLOW_PUSH_DEMO_CAMERA_MATCH (auto-select),\n"
        << "       RFLOW_PUSH_DEMO_MAX_FAILURES=N (exit 2 after N consecutive failures, 0=off)\n";
}

bool ParsePushDemoConfig(int argc, char** argv, PushDemoConfig& cfg) {
    if (argc < 2) {
        PrintPushDemoUsage(argv[0]);
        return false;
    }
    if (argc > 8) {
        std::cerr << "[demo] warning: extra arguments after [camera] are ignored\n";
    }

    cfg.signaling_url = argv[1];
    if (cfg.signaling_url.empty()) {
        std::cerr << "[demo] signaling_url must not be empty\n";
        PrintPushDemoUsage(argv[0]);
        return false;
    }

    if (argc >= 3) cfg.device_id = argv[2];
    if (argc >= 4) {
        if (!IsPositiveIntArg(argv[3])) {
            std::cerr << "[demo] invalid width: " << argv[3] << "\n";
            PrintPushDemoUsage(argv[0]);
            return false;
        }
        cfg.width = std::atoi(argv[3]);
    }
    if (argc >= 5) {
        if (!IsPositiveIntArg(argv[4])) {
            std::cerr << "[demo] invalid height: " << argv[4] << "\n";
            PrintPushDemoUsage(argv[0]);
            return false;
        }
        cfg.height = std::atoi(argv[4]);
    }
    if (argc >= 6) {
        if (!IsPositiveIntArg(argv[5])) {
            std::cerr << "[demo] invalid fps: " << argv[5] << "\n";
            PrintPushDemoUsage(argv[0]);
            return false;
        }
        cfg.fps = std::atoi(argv[5]);
    }
    if (argc >= 7) {
        if (!IsPositiveIntArg(argv[6])) {
            std::cerr << "[demo] invalid stream_idx: " << argv[6] << "\n";
            PrintPushDemoUsage(argv[0]);
            return false;
        }
        cfg.stream_idx = static_cast<rflow_stream_index_t>(std::atoi(argv[6]));
    }
    if (argc >= 8) {
        cfg.camera = argv[7];
#if defined(__linux__) && defined(RFLOW_APPS_PUSH_DEMO)
        if (!cfg.camera.empty() && !IsLinuxVideoCaptureReady(cfg.camera)) {
            std::cerr << "[demo] note: camera " << cfg.camera
                      << " not openable yet; will retry in main loop\n";
        }
#endif
    }

    if (cfg.width <= 0 || cfg.height <= 0 || cfg.fps <= 0) {
        std::cerr << "[demo] width, height, fps must be positive\n";
        PrintPushDemoUsage(argv[0]);
        return false;
    }
    if (cfg.stream_idx < 0) {
        std::cerr << "[demo] stream_idx must be >= 0\n";
        PrintPushDemoUsage(argv[0]);
        return false;
    }
    return true;
}

#if defined(__linux__) && defined(RFLOW_APPS_PUSH_DEMO)

void WarnIfCameraPathNotReady(const std::string& path) {
    if (path.empty()) return;
    if (!IsLinuxVideoCaptureReady(path)) {
        std::cerr << "[demo] warning: camera not openable yet: " << path
                  << " (will retry in main loop)\n";
    }
}

#endif

}  // namespace rflow::apps::common

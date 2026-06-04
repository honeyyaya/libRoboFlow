#ifndef __RFLOW_APPS_COMMON_DEMO_PUSH_SESSION_H__
#define __RFLOW_APPS_COMMON_DEMO_PUSH_SESSION_H__

// push_demo_sdk 专用：信令连接、推流 create/start/destroy、与相机热插拔协同调度。

#include "common/demo_camera_hotplug.h"
#include "rflow/Service/librflow_service_api.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

namespace rflow::apps::common {

struct PushDemoConfig {
    std::string signaling_url = "127.0.0.1:8765";
    std::string device_id     = "demo_device";
    int         width         = 1280;
    int         height        = 720;
    int         fps           = 60;
    rflow_stream_index_t stream_idx = 0;
    std::string camera;
};

class PushDemoSession {
 public:
    explicit PushDemoSession(PushDemoConfig config);

    PushDemoConfig&       config() { return config_; }
    const PushDemoConfig& config() const { return config_; }

#if defined(__linux__)
    CameraHotplugMonitor&       camera() { return *camera_; }
    const CameraHotplugMonitor& camera() const { return *camera_; }
#endif

    void set_shutting_down(bool v) { shutting_down_.store(v); }

    librflow_svc_stream_handle_t stream() const { return stream_; }

    bool stream_active() const { return stream_active_.load(); }

    bool want_stream_restart() const { return want_stream_restart_.load(); }

    void set_want_stream_restart(bool v) { want_stream_restart_.store(v); }

    void clear_want_stream_restart() { want_stream_restart_.store(false); }

    // --- SDK 回调（userdata = PushDemoSession*）---
    static void OnConnectState(rflow_connect_state_t state, rflow_err_t reason, void* ud);
    static void OnBindState(rflow_bind_state_t state, const char* detail, void* ud);
    static void OnPullRequest(rflow_stream_index_t idx, void* ud);
    static void OnPullRelease(rflow_stream_index_t idx, void* ud);
    static void OnStreamState(librflow_svc_stream_handle_t h, rflow_stream_state_t state,
                              rflow_err_t reason, void* ud);

    void HandleConnectState(rflow_connect_state_t state, rflow_err_t reason);
    void HandleStreamState(rflow_stream_state_t state, rflow_err_t reason);

    void RegisterConnectCallbacks(librflow_svc_connect_cb_t cb);

    // 填充 connect_info（device_id / product_key 等）。
    static void FillConnectInfo(librflow_svc_connect_info_t info, const PushDemoConfig& cfg);

    // 连接信令；回调由 SDK 在 librflow_svc_connect 内同步拷贝并触发 on_state。
    bool Connect();

    bool TryReconnectSignaling(std::chrono::steady_clock::time_point now);

    bool TryStartStream();

    void TeardownStream();

    // 每轮主循环：信令/相机/流重建调度。
    void ServiceTick(std::chrono::steady_clock::time_point now);

    void StopStreamForExit();

    bool exit_due_to_failures() const { return exit_due_to_failures_; }

 private:
    static constexpr int kStreamRestartBackoffMs  = 2000;
    static constexpr int kConnectRestartBackoffMs = 3000;
    static constexpr int kWarnEveryFailures       = 10;

    void NoteStreamStartFailure();
    void NoteConnectFailure();
    void CheckFailureExitThreshold();

    bool AttemptSignalingConnect();

    librflow_svc_stream_param_t MakeStreamParam() const;

    PushDemoConfig config_;

    std::atomic<rflow_connect_state_t> conn_state_{RFLOW_CONN_IDLE};
    std::atomic<bool> stream_active_{false};
    std::atomic<bool> want_stream_restart_{false};
    std::atomic<bool> want_connect_restart_{false};
    std::atomic<bool> shutting_down_{false};

    librflow_svc_stream_handle_t stream_{nullptr};
    std::chrono::steady_clock::time_point next_stream_attempt_{};
    std::chrono::steady_clock::time_point next_connect_attempt_{};
    int consecutive_stream_failures_{0};
    int consecutive_connect_failures_{0};
    int  max_failures_exit_{0};
    bool exit_due_to_failures_{false};

#if defined(__linux__)
    std::unique_ptr<CameraHotplugMonitor> camera_;
#endif
};

}  // namespace rflow::apps::common

#endif  // __RFLOW_APPS_COMMON_DEMO_PUSH_SESSION_H__

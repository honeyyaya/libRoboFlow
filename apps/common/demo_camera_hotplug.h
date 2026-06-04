#ifndef __RFLOW_APPS_COMMON_DEMO_CAMERA_HOTPLUG_H__
#define __RFLOW_APPS_COMMON_DEMO_CAMERA_HOTPLUG_H__

// Linux USB 相机热插拔：libudev video4linux 监听 + 节点探测/核对兜底。
// 仅面向 push demo；不依赖 librflow_svc。

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace rflow::apps::common {

struct CameraHotplugConfig {
    std::string dev_path;
    int stable_ms           = 1500;
    int fallback_probe_ms   = 2000;
    int safety_reconcile_ms = 5000;
    int main_loop_poll_ms   = 200;
};

class CameraHotplugMonitor {
 public:
    using DevPathChangedCallback = std::function<void(const std::string& new_path)>;

    explicit CameraHotplugMonitor(CameraHotplugConfig config);
    ~CameraHotplugMonitor();

    CameraHotplugMonitor(const CameraHotplugMonitor&)            = delete;
    CameraHotplugMonitor& operator=(const CameraHotplugMonitor&) = delete;

    void SetOnDevPathChanged(DevPathChangedCallback cb);

    void SyncInitialPresence();

    bool Start();

    void Stop();

    void PollWait(std::chrono::steady_clock::time_point now, bool stream_active);

    bool IsPresent() const { return present_last_; }

    bool IsStableAndReady(std::chrono::steady_clock::time_point now) const;

    bool RestartRequested() const { return restart_requested_; }

    void ClearRestartRequest() { restart_requested_ = false; }

    bool UsesUdev() const;

 private:
    void ApplyPresence(bool present, std::chrono::steady_clock::time_point now, const char* via);

#if defined(__linux__)
    void AdoptDevPath(const std::string& new_path, const char* via,
                      std::chrono::steady_clock::time_point now);
    void HandleUdevDevice(void* udev_dev, const char* action,
                          std::chrono::steady_clock::time_point now);
#endif

    CameraHotplugConfig config_;
    DevPathChangedCallback on_path_changed_;
    bool present_last_{false};
    bool restart_requested_{false};
    bool use_udev_{false};
    std::chrono::steady_clock::time_point present_since_{};
    std::chrono::steady_clock::time_point last_fallback_probe_{};
    std::chrono::steady_clock::time_point last_safety_reconcile_{};

#if defined(__linux__)
    std::string device_identity_;
#endif

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rflow::apps::common

#endif  // __RFLOW_APPS_COMMON_DEMO_CAMERA_HOTPLUG_H__

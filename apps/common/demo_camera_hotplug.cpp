#include "common/demo_camera_hotplug.h"

#include "common/demo_v4l_linux.h"

#include <functional>
#include <iostream>
#include <thread>

#if defined(__linux__)
#  include <cstring>
#  include <fcntl.h>
#  include <libudev.h>
#  include <poll.h>
#  include <unistd.h>
#endif

namespace rflow::apps::common {

#if defined(__linux__)

namespace {

bool IsCameraNodePresent(const std::string& path) {
    return !path.empty() && access(path.c_str(), F_OK) == 0;
}

bool IsV4lCaptureCapable(struct udev_device* dev) {
    const char* caps = udev_device_get_property_value(dev, "ID_V4L_CAPABILITIES");
    return caps && std::strstr(caps, "capture") != nullptr;
}

class LinuxLibudevMonitor {
 public:
    ~LinuxLibudevMonitor() { Close(); }

    bool Open() {
        Close();

        udev_ = udev_new();
        if (!udev_) {
            std::cerr << "[demo][udev] udev_new failed\n";
            return false;
        }

        mon_ = udev_monitor_new_from_netlink(udev_, "udev");
        if (!mon_) {
            std::cerr << "[demo][udev] udev_monitor_new_from_netlink failed\n";
            Close();
            return false;
        }

        if (udev_monitor_filter_add_match_subsystem_devtype(mon_, "video4linux", nullptr) != 0) {
            std::cerr << "[demo][udev] filter video4linux failed\n";
            Close();
            return false;
        }

        if (udev_monitor_enable_receiving(mon_) != 0) {
            std::cerr << "[demo][udev] enable_receiving failed\n";
            Close();
            return false;
        }

        fd_ = udev_monitor_get_fd(mon_);
        if (fd_ < 0) {
            std::cerr << "[demo][udev] monitor fd invalid\n";
            Close();
            return false;
        }

        const int flags = fcntl(fd_, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
        }
        return true;
    }

    void Close() {
        if (mon_) {
            udev_monitor_unref(mon_);
            mon_ = nullptr;
        }
        if (udev_) {
            udev_unref(udev_);
            udev_ = nullptr;
        }
        fd_ = -1;
    }

    int Fd() const { return fd_; }

    void Drain(const std::function<void(struct udev_device*, const char*)>& on_device) {
        if (!mon_) return;
        for (;;) {
            struct udev_device* dev = udev_monitor_receive_device(mon_);
            if (!dev) break;
            on_device(dev, udev_device_get_action(dev));
            udev_device_unref(dev);
        }
    }

 private:
    struct udev*         udev_{nullptr};
    struct udev_monitor* mon_{nullptr};
    int                  fd_{-1};
};

}  // namespace

struct CameraHotplugMonitor::Impl {
    LinuxLibudevMonitor udev;
};

void CameraHotplugMonitor::AdoptDevPath(const std::string& new_path, const char* via,
                                      std::chrono::steady_clock::time_point now) {
    if (new_path.empty() || new_path == config_.dev_path) return;
    std::cout << "[demo][camera] devnode migrated (" << via << "): " << config_.dev_path << " -> "
              << new_path << std::endl;
    config_.dev_path = new_path;
    if (on_path_changed_) {
        on_path_changed_(new_path);
    }
    present_last_  = true;
    present_since_ = now;
    restart_requested_ = true;
}

void CameraHotplugMonitor::HandleUdevDevice(void* udev_dev, const char* action,
                                            std::chrono::steady_clock::time_point now) {
    auto* dev = static_cast<struct udev_device*>(udev_dev);
    if (!action || !dev) return;
    const char* devnode   = udev_device_get_devnode(dev);
    const char* subsystem = udev_device_get_subsystem(dev);
    if (!devnode || !subsystem || std::strcmp(subsystem, "video4linux") != 0) return;
    if (!IsV4lCaptureCapable(dev)) return;

    if (std::strcmp(action, "remove") == 0) {
        if (config_.dev_path == devnode) {
            ApplyPresence(false, now, "udev-remove");
        }
        return;
    }

    if (std::strcmp(action, "add") != 0) return;

    if (config_.dev_path == devnode) {
        ApplyPresence(true, now, "udev-add");
        return;
    }

    if (device_identity_.empty()) return;
    const std::string new_id = LinuxCaptureDeviceIdentity(devnode);
    if (new_id.empty() || new_id != device_identity_) return;
    if (!IsLinuxVideoCaptureReady(devnode)) return;

    AdoptDevPath(devnode, "udev-migrate", now);
}

#endif  // __linux__

CameraHotplugMonitor::CameraHotplugMonitor(CameraHotplugConfig config) : config_(std::move(config)) {
#if defined(__linux__)
    impl_ = std::make_unique<Impl>();
#endif
}

CameraHotplugMonitor::~CameraHotplugMonitor() {
    Stop();
}

void CameraHotplugMonitor::SetOnDevPathChanged(DevPathChangedCallback cb) {
    on_path_changed_ = std::move(cb);
}

bool CameraHotplugMonitor::UsesUdev() const { return use_udev_; }

void CameraHotplugMonitor::ApplyPresence(bool present, std::chrono::steady_clock::time_point now,
                                        const char* via) {
    if (present == present_last_) return;
    present_last_ = present;
    if (present) {
        present_since_ = now;
        std::cout << "[demo][camera] online (" << via << "): " << config_.dev_path << std::endl;
    } else {
        present_since_ = {};
        std::cout << "[demo][camera] offline (" << via << "): " << config_.dev_path << std::endl;
    }
    restart_requested_ = true;
}

void CameraHotplugMonitor::SyncInitialPresence() {
#if defined(__linux__)
    device_identity_ = LinuxCaptureDeviceIdentity(config_.dev_path);
    const bool present = IsLinuxVideoCaptureReady(config_.dev_path);
    present_last_      = present;
    present_since_     = present ? std::chrono::steady_clock::now()
                                 : std::chrono::steady_clock::time_point{};
    if (!present) {
        restart_requested_ = true;
        std::cout << "[demo][camera] waiting for device " << config_.dev_path << " ..." << std::endl;
    }
#else
    present_last_ = true;
#endif
}

bool CameraHotplugMonitor::Start() {
#if defined(__linux__)
    use_udev_ = impl_ != nullptr && impl_->udev.Open();
    if (!use_udev_) {
        std::cerr << "[demo][udev] libudev unavailable; using node probe every "
                  << config_.fallback_probe_ms << "ms" << std::endl;
    } else {
        std::cout << "[demo][udev] libudev monitor video4linux identity="
                  << (device_identity_.empty() ? "(none)" : device_identity_)
                  << " devnode=" << config_.dev_path << std::endl;
    }
    const auto now     = std::chrono::steady_clock::now();
    last_fallback_probe_   = now;
    last_safety_reconcile_ = now;
    if (use_udev_) {
        impl_->udev.Drain([this](struct udev_device* dev, const char* action) {
            HandleUdevDevice(dev, action, std::chrono::steady_clock::now());
        });
    }
    return use_udev_;
#else
    return false;
#endif
}

void CameraHotplugMonitor::Stop() {
#if defined(__linux__)
    if (impl_) {
        impl_->udev.Close();
    }
#endif
    use_udev_ = false;
}

void CameraHotplugMonitor::PollWait(std::chrono::steady_clock::time_point now, bool stream_active) {
#if defined(__linux__)
    if (use_udev_ && impl_ != nullptr && impl_->udev.Fd() >= 0) {
        struct pollfd pfd {};
        pfd.fd     = impl_->udev.Fd();
        pfd.events = POLLIN;
        const int pr = poll(&pfd, 1, config_.main_loop_poll_ms);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            impl_->udev.Drain([this](struct udev_device* dev, const char* action) {
                HandleUdevDevice(dev, action, std::chrono::steady_clock::now());
            });
        }
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_safety_reconcile_)
                .count() >= config_.safety_reconcile_ms) {
            last_safety_reconcile_ = now;
            if (stream_active && present_last_ && !IsCameraNodePresent(config_.dev_path)) {
                ApplyPresence(false, now, "reconcile");
            }
        }
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.main_loop_poll_ms));
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fallback_probe_)
                .count() >= config_.fallback_probe_ms) {
            last_fallback_probe_ = now;
            ApplyPresence(IsCameraNodePresent(config_.dev_path), now, "probe");
        }
    }
#else
    (void)now;
    (void)stream_active;
    std::this_thread::sleep_for(std::chrono::milliseconds(config_.main_loop_poll_ms));
#endif
}

bool CameraHotplugMonitor::IsStableAndReady(std::chrono::steady_clock::time_point now) const {
#if defined(__linux__)
    if (!present_last_) return false;
    auto since = present_since_;
    if (since == std::chrono::steady_clock::time_point{}) {
        since = now;
    }
    const auto stable_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - since).count();
    if (stable_ms < config_.stable_ms) return false;
    return IsLinuxVideoCaptureReady(config_.dev_path);
#else
    (void)now;
    return true;
#endif
}

}  // namespace rflow::apps::common

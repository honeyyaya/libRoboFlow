#include "common/demo_v4l_linux.h"

#if defined(__linux__)

#  include <algorithm>
#  include <cctype>
#  include <cstdio>
#  include <cstdlib>
#  include <cstring>
#  include <fcntl.h>
#  include <iostream>
#  include <libudev.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  include <vector>

namespace rflow::apps::common {

namespace {

bool IsV4lCaptureCapable(struct udev_device* dev) {
    const char* caps = udev_device_get_property_value(dev, "ID_V4L_CAPABILITIES");
    return caps && std::strstr(caps, "capture") != nullptr;
}

int VideoIndexFromPath(const char* devnode) {
    if (!devnode) return -1;
    unsigned idx = 0;
    if (std::sscanf(devnode, "/dev/video%u", &idx) == 1) {
        return static_cast<int>(idx);
    }
    return -1;
}

std::string IdentityFromUdevDevice(struct udev_device* dev) {
    if (!dev) return {};
    const char* serial = udev_device_get_property_value(dev, "ID_SERIAL_SHORT");
    if (!serial || !serial[0]) {
        serial = udev_device_get_property_value(dev, "ID_SERIAL");
    }
    if (serial && serial[0]) {
        return std::string("serial:") + serial;
    }
    const char* devpath = udev_device_get_devpath(dev);
    if (devpath && devpath[0]) {
        return std::string("devpath:") + devpath;
    }
    return {};
}

int ScoreCaptureDevice(struct udev_device* dev) {
    if (!dev) return -1;
    int score = 0;
    const char* bus = udev_device_get_property_value(dev, "ID_BUS");
    if (bus && std::strcmp(bus, "usb") == 0) score += 50;

    const char* model = udev_device_get_property_value(dev, "ID_MODEL");
    if (model) {
        std::string m(model);
        for (auto& c : m) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (m.find("camera") != std::string::npos) score += 80;
        if (const char* prefer = std::getenv("RFLOW_PUSH_DEMO_CAMERA_MATCH")) {
            if (prefer[0] != '\0' && m.find(prefer) != std::string::npos) score += 150;
        }
    }

    const char* driver = udev_device_get_property_value(dev, "ID_USB_DRIVER");
    if (driver && std::strcmp(driver, "uvcvideo") == 0) score += 20;

    const char* path = udev_device_get_devnode(dev);
    if (path && std::strstr(path, "video-dec") != nullptr) score -= 1000;
    return score;
}

struct CaptureCandidate {
    int         score{0};
    int         index{0};
    std::string path;
};

}  // namespace

bool IsLinuxVideoCaptureReady(const std::string& path) {
    if (path.empty()) return false;
    if (access(path.c_str(), F_OK) != 0) return false;
    const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) return false;
    close(fd);
    return true;
}

std::string LinuxCaptureDeviceIdentity(const std::string& dev_path) {
    if (dev_path.empty()) return {};
    struct stat st {};
    if (stat(dev_path.c_str(), &st) != 0 || !S_ISCHR(st.st_mode)) return {};

    struct udev* udev = udev_new();
    if (!udev) return {};
    struct udev_device* dev =
        udev_device_new_from_devnum(udev, 'c', static_cast<dev_t>(st.st_rdev));
    if (!dev) {
        udev_unref(udev);
        return {};
    }
    const std::string id = IdentityFromUdevDevice(dev);
    udev_device_unref(dev);
    udev_unref(udev);
    return id;
}

std::string ProbeDefaultLinuxCaptureDevice() {
    struct udev* udev = udev_new();
    if (!udev) {
        std::cerr << "[demo] udev_new failed during camera probe\n";
        return "/dev/video0";
    }

    struct udev_enumerate* enumerate = udev_enumerate_new(udev);
    if (!enumerate) {
        udev_unref(udev);
        return "/dev/video0";
    }

    udev_enumerate_add_match_subsystem(enumerate, "video4linux");
    udev_enumerate_scan_devices(enumerate);

    std::vector<CaptureCandidate> candidates;
    struct udev_list_entry* entry = udev_enumerate_get_list_entry(enumerate);
    for (; entry != nullptr; entry = udev_list_entry_get_next(entry)) {
        const char* syspath = udev_list_entry_get_name(entry);
        if (!syspath) continue;

        struct udev_device* dev = udev_device_new_from_syspath(udev, syspath);
        if (!dev) continue;

        const char* devnode = udev_device_get_devnode(dev);
        if (devnode && IsV4lCaptureCapable(dev)) {
            const int idx = VideoIndexFromPath(devnode);
            if (idx >= 0) {
                CaptureCandidate c;
                c.score = ScoreCaptureDevice(dev);
                c.index = idx;
                c.path  = devnode;
                candidates.push_back(std::move(c));
            }
        }
        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
    udev_unref(udev);

    std::sort(candidates.begin(), candidates.end(), [](const CaptureCandidate& a,
                                                       const CaptureCandidate& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.index < b.index;
    });

    for (const auto& c : candidates) {
        if (c.score < 0) continue;
        if (IsLinuxVideoCaptureReady(c.path)) {
            std::cout << "[demo] auto-selected camera " << c.path << " (score=" << c.score << ")"
                      << std::endl;
            return c.path;
        }
    }

    std::cerr << "[demo] no udev capture device openable; fallback /dev/video0 "
                 "(set RFLOW_PUSH_DEMO_CAMERA or argv[7])\n";
    return "/dev/video0";
}

}  // namespace rflow::apps::common

#endif  // __linux__

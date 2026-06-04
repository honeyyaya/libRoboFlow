#ifndef __RFLOW_APPS_COMMON_DEMO_V4L_LINUX_H__
#define __RFLOW_APPS_COMMON_DEMO_V4L_LINUX_H__

// push_demo_sdk 专用：Linux V4L 采集节点探测（libudev + open 校验）。

#include <string>

namespace rflow::apps::common {

#if defined(__linux__)

bool IsLinuxVideoCaptureReady(const std::string& path);

// 物理设备身份（序列号 / devpath），用于热插拔后节点号变化时仍视为同一相机。
std::string LinuxCaptureDeviceIdentity(const std::string& dev_path);

std::string ProbeDefaultLinuxCaptureDevice();

#endif

}  // namespace rflow::apps::common

#endif  // __RFLOW_APPS_COMMON_DEMO_V4L_LINUX_H__

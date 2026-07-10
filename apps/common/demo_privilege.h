#ifndef RFLOW_APPS_COMMON_DEMO_PRIVILEGE_H_
#define RFLOW_APPS_COMMON_DEMO_PRIVILEGE_H_

namespace rflow::apps::common {

/// 若进程由 sudo 启动（存在 SUDO_UID），从 root 降权到原用户并补全 video/render 组，
/// 避免 MPP 硬解/WebRTC 在 root 下异常。返回 true 表示已降权。
/// 设置 RFLOW_ALLOW_ROOT=1 可跳过（仅调试）。
bool DropRootPrivilegesIfSudoInvoked();

}  // namespace rflow::apps::common

#endif  // RFLOW_APPS_COMMON_DEMO_PRIVILEGE_H_

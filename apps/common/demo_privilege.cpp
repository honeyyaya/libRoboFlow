#include "common/demo_privilege.h"

#if defined(__linux__)

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <grp.h>
#include <pwd.h>
#include <unistd.h>

namespace rflow::apps::common {

namespace {

constexpr int kMaxGroups = 64;

bool EnvTruthy(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] && v[0] != '0' && v[0] != 'n' && v[0] != 'N' && v[0] != 'f' && v[0] != 'F';
}

gid_t GidByName(const char* name) {
    struct group* g = getgrnam(name);
    return g ? g->gr_gid : static_cast<gid_t>(-1);
}

void AppendGid(std::vector<gid_t>* groups, gid_t gid) {
    if (gid == static_cast<gid_t>(-1)) {
        return;
    }
    if (std::find(groups->begin(), groups->end(), gid) == groups->end()) {
        groups->push_back(gid);
    }
}

}  // namespace

bool DropRootPrivilegesIfSudoInvoked() {
    if (geteuid() != 0) {
        return false;
    }
    if (EnvTruthy("RFLOW_ALLOW_ROOT")) {
        std::fprintf(stderr,
                     "[demo] RFLOW_ALLOW_ROOT=1: keeping root (MPP/WebRTC may be unstable).\n");
        return false;
    }

    const char* sudo_uid = std::getenv("SUDO_UID");
    if (!sudo_uid || !sudo_uid[0]) {
        std::fprintf(stderr,
                     "[demo] warning: running as root without SUDO_UID; use normal user, "
                     "'sudo -u user', or setcap cap_sys_nice on push_demo_sdk.\n");
        return false;
    }

    const uid_t uid = static_cast<uid_t>(std::atoi(sudo_uid));
    const char* sudo_gid = std::getenv("SUDO_GID");
    const gid_t primary_gid =
        (sudo_gid && sudo_gid[0]) ? static_cast<gid_t>(std::atoi(sudo_gid)) : uid;

    struct passwd* pw = getpwuid(uid);
    if (!pw || !pw->pw_name) {
        std::fprintf(stderr, "[demo] getpwuid(%u) failed; cannot drop root.\n", static_cast<unsigned>(uid));
        return false;
    }

    gid_t group_buf[kMaxGroups];
    int ngroups = kMaxGroups;
    if (getgrouplist(pw->pw_name, primary_gid, group_buf, &ngroups) < 0) {
        std::fprintf(stderr, "[demo] getgrouplist for %s failed; cannot drop root.\n", pw->pw_name);
        return false;
    }

    std::vector<gid_t> groups(group_buf, group_buf + ngroups);
    AppendGid(&groups, GidByName("video"));
    AppendGid(&groups, GidByName("render"));

    if (setgroups(static_cast<int>(groups.size()), groups.data()) != 0 ||
        setgid(primary_gid) != 0 || setuid(uid) != 0) {
        std::fprintf(stderr, "[demo] drop root -> uid=%u failed errno=%d\n", static_cast<unsigned>(uid),
                     errno);
        return false;
    }

    std::fprintf(stderr,
                 "[demo] dropped root privileges -> uid=%u gid=%u groups=%zu (video/render ensured)\n",
                 static_cast<unsigned>(uid), static_cast<unsigned>(primary_gid), groups.size());
    return true;
}

}  // namespace rflow::apps::common

#else  // !__linux__

namespace rflow::apps::common {

bool DropRootPrivilegesIfSudoInvoked() { return false; }

}  // namespace rflow::apps::common

#endif

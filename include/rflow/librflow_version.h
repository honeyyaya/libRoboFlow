#ifndef __LIBRFLOW_VERSION_H__
#define __LIBRFLOW_VERSION_H__

/* 语义版本号：主.次.补丁；组合成 32bit = 1B MAJOR | 1B MINOR | 2B PATCH */
#define LIBRFLOW_VERSION_MAJOR 0
#define LIBRFLOW_VERSION_MINOR 9
#define LIBRFLOW_VERSION_PATCH 4

/* 兼容旧命名 */
#define LIBRFLOW_MAJOR LIBRFLOW_VERSION_MAJOR
#define LIBRFLOW_MINOR LIBRFLOW_VERSION_MINOR
#define LIBRFLOW_MICRO LIBRFLOW_VERSION_PATCH

#define LIBRFLOW_VERSION_INT(major, minor, patch) \
    (((major) << 24) | ((minor) << 16) | ((patch) & 0xFFFF))

#define LIBRFLOW_VERSION LIBRFLOW_VERSION_INT(LIBRFLOW_VERSION_MAJOR, \
                                              LIBRFLOW_VERSION_MINOR, \
                                              LIBRFLOW_VERSION_PATCH)

#endif //__LIBRFLOW_VERSION_H__

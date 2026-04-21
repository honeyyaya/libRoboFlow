#ifndef __LIBROBRT_VERSION_H__
#define __LIBROBRT_VERSION_H__

/* 语义版本号：主.次.补丁；组合成 32bit = 1B MAJOR | 1B MINOR | 2B PATCH */
#define LIBROBRT_VERSION_MAJOR 0
#define LIBROBRT_VERSION_MINOR 9
#define LIBROBRT_VERSION_PATCH 4

/* 兼容旧命名 */
#define LIBROBRT_MAJOR LIBROBRT_VERSION_MAJOR
#define LIBROBRT_MINOR LIBROBRT_VERSION_MINOR
#define LIBROBRT_MICRO LIBROBRT_VERSION_PATCH

#define LIBROBRT_VERSION_INT(major, minor, patch) \
    (((major) << 24) | ((minor) << 16) | ((patch) & 0xFFFF))

#define LIBROBRT_VERSION LIBROBRT_VERSION_INT(LIBROBRT_VERSION_MAJOR, \
                                              LIBROBRT_VERSION_MINOR, \
                                              LIBROBRT_VERSION_PATCH)

/*
 * 用于 #if 条件编译，例如：
 *   #if LIBROBRT_VERSION >= LIBROBRT_VERSION_INT(1, 0, 0)
 *     librobrt_xxx_v2(...)
 *   #else
 *     librobrt_xxx(...)
 *   #endif
 */

#define COMMIT_VERSION  "e312ab1c"
#define COMMIT_TIME     "2025-11-17 15:40:06"
#define BUILD_TIME      "2025-11-18T14:43:37"

#endif //__LIBROBRT_VERSION_H__

/**
 * @file     librobrt_typedef.h
 * @brief    类型定义
 *
 * 官方构建支持：GCC / Clang（Linux arm64、Android arm64）。
 * 该头文件允许在任意 C/C++ 编译器下被消费端 include（仅声明，不实际链接）；
 * 非 GCC/Clang 下 LIBROBRT_API_EXPORT 与 LIBROBRT_API_DEPRECATED_MSG 退化为空，
 * 以便跨平台项目能共享同一份头文件进行静态检查或生成 FFI binding。
**/

#ifndef __LIBROBRT_TYPEDEF_H__
#define __LIBROBRT_TYPEDEF_H__

#include <stdbool.h>
#include <stdint.h>

#ifndef LIBROBRT_API_DEPRECATED_MSG
#if defined(__GNUC__) || defined(__clang__)
#define LIBROBRT_API_DEPRECATED_MSG(func) \
    __attribute__((__deprecated__("Use " func " instead")))
#elif defined(_MSC_VER)
#define LIBROBRT_API_DEPRECATED_MSG(func) __declspec(deprecated("Use " func " instead"))
#else
#define LIBROBRT_API_DEPRECATED_MSG(func)
#endif
#endif

#ifdef __cplusplus
#ifndef LIBROBRT_API_EXPORT
#define LIBROBRT_API_EXPORT extern "C"
#endif
#ifndef LIBROBRT_DEPRECATED_EXPORT
#define LIBROBRT_DEPRECATED_EXPORT(func) extern "C" LIBROBRT_API_DEPRECATED_MSG(func)
#endif
#else
#ifndef LIBROBRT_API_EXPORT
#define LIBROBRT_API_EXPORT
#endif
#ifndef LIBROBRT_DEPRECATED_EXPORT
#define LIBROBRT_DEPRECATED_EXPORT(func) LIBROBRT_API_DEPRECATED_MSG(func)
#endif
#endif

/*
 * 对外 API 统一使用 <stdint.h> / <stdbool.h> 的标准类型
 * (int32_t / uint32_t / bool / ...)，不再暴露 LIBROBRT_* 别名。
 */

#endif /* __LIBROBRT_TYPEDEF_H__ */

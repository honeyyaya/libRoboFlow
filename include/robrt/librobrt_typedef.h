/**
 * @file     librobrt_typedef.h
 * @brief    类型定义
**/

#ifndef __LIBROBRT_TYPEDEF_H__
#define __LIBROBRT_TYPEDEF_H__

/**************************** Include ****************************/
#include <stdbool.h>
#include <stdint.h>
#if defined(_MSC_VER)
#include <winsock2.h>
#include <windows.h>
#endif

/***************************** Macro *****************************/

#ifdef __GNUC__
    #ifndef LIBROBRT_API_DEPRECATED_MSG
    #define LIBROBRT_API_DEPRECATED_MSG(func) __attribute__ ((__deprecated__("Use " func " instead")))
    #endif
#elif _MSC_VER
    #ifndef LIBROBRT_API_DEPRECATED_MSG
    #define LIBROBRT_API_DEPRECATED_MSG(func) __declspec(deprecated("Use " func " instead"))
    #endif
#else 
#error "not supprt compiler"
#endif

#ifdef __cplusplus
    #ifndef LIBROBRT_API_EXPORT
    #define LIBROBRT_API_EXPORT extern "C"
    #endif
    #ifndef LIBROBRT_DEPRECATED_EXPORT
    #define LIBROBRT_DEPRECATED_EXPORT(func) extern "C"  LIBROBRT_API_DEPRECATED_MSG(func)
    #endif
#else
    #ifndef LIBROBRT_API_EXPORT
    #define LIBROBRT_API_EXPORT
    #endif
    #ifndef LIROBRT_DEPRECATED_EXPORT
    #define LIROBRT_DEPRECATED_EXPORT(func) LIBROBRT_API_DEPRECATED_MSG(func) 
    #endif
#endif


#ifdef __GNUC__
    typedef void            LIBROBRT_VOID;
    typedef void*           LIBROBRT_HANDLE;
    typedef bool            LIBROBRT_BOOL;
    typedef char            LIBROBRT_CHAR;
    typedef unsigned char   LIBROBRT_UCHAR;
    typedef short           LIBROBRT_SHORT;
    typedef unsigned short  LIBROBRT_USHORT;
    typedef int             LIBROBRT_INT;
    typedef unsigned int    LIBROBRT_UINT;
    typedef int16_t         LIBROBRT_INT16;
    typedef uint16_t        LIBROBRT_UINT16;
    typedef int32_t         LIBROBRT_INT32;
    typedef uint32_t        LIBROBRT_UINT32;
    typedef int64_t         LIBROBRT_INT64;
    typedef int64_t         LIBROBRT_UINT64;
    typedef long            LIBROBRT_LONG;
    typedef unsigned long   LIBROBRT_ULONG;
    typedef float           LIBROBRT_FLOAT;
    typedef double          LIBROBRT_DOUBLE;
#else
#error "not supprt compiler"
#endif
#endif /* __LIBROBRT_TYPEDEF_H__ */

#ifndef __RFLOW_COMMON_ABI_OBJECT_ACCESS_OPS_H__
#define __RFLOW_COMMON_ABI_OBJECT_ACCESS_OPS_H__

#include "common/abi/handle.h"

#include <cstdint>
#include <new>

namespace rflow::common::abi {

// 申请 T 对象（默认构造），并设置 magic；分配失败返回 nullptr。
template <typename T>
inline T* CreateMagicObject(uint32_t magic) {
    auto* obj = new (std::nothrow) T();
    if (!obj) return nullptr;
    obj->magic = magic;
    return obj;
}

// 校验 magic，匹配则置 0 + delete；不匹配则什么都不做（防双 free / 传错指针）。
template <typename T>
inline void DestroyMagicObject(T* obj, uint32_t magic) {
    if (!obj || obj->magic != magic) return;
    obj->magic = 0;
    delete obj;
}

}  // namespace rflow::common::abi

#define RFLOW_SET_FIELD(cb, magic_value, field_name, value_expr) \
    do {                                                          \
        RFLOW_CHECK_HANDLE(cb, magic_value);                      \
        cb->field_name = (value_expr);                            \
        return RFLOW_OK;                                          \
    } while (0)

#define RFLOW_SET_VALUE_WITH_FLAG(obj, magic_value, field_name, has_field_name, value_expr) \
    do {                                                                                      \
        RFLOW_CHECK_HANDLE(obj, magic_value);                                                 \
        (obj)->field_name = (value_expr);                                                     \
        (obj)->has_field_name = true;                                                         \
        return RFLOW_OK;                                                                      \
    } while (0)

#define RFLOW_GET_VALUE_WITH_FLAG(obj, magic_value, out_ptr, has_field_name, value_expr) \
    do {                                                                                   \
        RFLOW_CHECK_HANDLE(obj, magic_value);                                              \
        if (!(out_ptr)) return RFLOW_ERR_PARAM;                                            \
        if (!(obj)->has_field_name) return RFLOW_ERR_NOT_FOUND;                            \
        *(out_ptr) = (value_expr);                                                         \
        return RFLOW_OK;                                                                    \
    } while (0)

#define RFLOW_SET_2_VALUES_WITH_FLAG(                                                      \
    obj, magic_value, field_name1, value_expr1, field_name2, value_expr2, has_field_name) \
    do {                                                                                    \
        RFLOW_CHECK_HANDLE(obj, magic_value);                                               \
        (obj)->field_name1 = (value_expr1);                                                 \
        (obj)->field_name2 = (value_expr2);                                                 \
        (obj)->has_field_name = true;                                                       \
        return RFLOW_OK;                                                                    \
    } while (0)

#define RFLOW_GET_2_VALUES_WITH_FLAG(                                               \
    obj, magic_value, out_ptr1, out_ptr2, has_field_name, value_expr1, value_expr2) \
    do {                                                                             \
        RFLOW_CHECK_HANDLE(obj, magic_value);                                        \
        if (!(out_ptr1) || !(out_ptr2)) return RFLOW_ERR_PARAM;                     \
        if (!(obj)->has_field_name) return RFLOW_ERR_NOT_FOUND;                     \
        *(out_ptr1) = (value_expr1);                                                 \
        *(out_ptr2) = (value_expr2);                                                 \
        return RFLOW_OK;                                                             \
    } while (0)

#define RFLOW_SET_3_VALUES_WITH_FLAG(                                                                 \
    obj, magic_value, field_name1, value_expr1, field_name2, value_expr2, field_name3, value_expr3, \
    has_field_name)                                                                                   \
    do {                                                                                               \
        RFLOW_CHECK_HANDLE(obj, magic_value);                                                          \
        (obj)->field_name1 = (value_expr1);                                                            \
        (obj)->field_name2 = (value_expr2);                                                            \
        (obj)->field_name3 = (value_expr3);                                                            \
        (obj)->has_field_name = true;                                                                  \
        return RFLOW_OK;                                                                               \
    } while (0)

#define RFLOW_GET_3_VALUES_WITH_FLAG(                                                      \
    obj, magic_value, out_ptr1, out_ptr2, out_ptr3, has_field_name, value_expr1,          \
    value_expr2, value_expr3)                                                               \
    do {                                                                                     \
        RFLOW_CHECK_HANDLE(obj, magic_value);                                                \
        if (!(out_ptr1) || !(out_ptr2) || !(out_ptr3)) return RFLOW_ERR_PARAM;             \
        if (!(obj)->has_field_name) return RFLOW_ERR_NOT_FOUND;                             \
        *(out_ptr1) = (value_expr1);                                                         \
        *(out_ptr2) = (value_expr2);                                                         \
        *(out_ptr3) = (value_expr3);                                                         \
        return RFLOW_OK;                                                                     \
    } while (0)

#endif  // __RFLOW_COMMON_ABI_OBJECT_ACCESS_OPS_H__

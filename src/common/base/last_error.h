#ifndef __RFLOW_COMMON_BASE_LAST_ERROR_H__
#define __RFLOW_COMMON_BASE_LAST_ERROR_H__

#include <cstdint>
#include <string>

#include "rflow/librflow_common.h"

namespace rflow {

/// 简易写入：只更新 message 字段；code 与 origin 保持 set_last_error_full 上次写入时的值。
/// 兼容历史调用方（"set_last_error('xxx')" 不带其他元数据）。
void set_last_error(const std::string& msg);
void set_last_error(const char* msg);

/// 增量写入（带 origin 标签）：在 set_last_error 基础上把 origin 一并替换。
/// 推荐每个 .cpp 在 anonymous namespace 里定义一个 kErrorOrigin 字符串字面量，
/// 然后把 set_last_error("xxx") 批量替换为 set_last_error("xxx", kErrorOrigin)，
/// origin 走内部 get_last_error_snapshot() 给上层日志/排障定位子系统使用，
/// 不通过 C ABI 对外。
void set_last_error(const std::string& msg, const char* origin);
void set_last_error(const char* msg, const char* origin);

/// 增量写入（同时覆盖 code/origin）：当调用现场已知具体错误码时使用，
/// 等价于 set_last_error_full(code, msg, origin)，但参数顺序更接近原 set_last_error。
void set_last_error(rflow_err_t code, const std::string& msg, const char* origin);

/// 结构化写入：覆盖整组字段；origin 可为空字符串（不限定子系统）。
/// timestamp_ms 由实现内部用 chrono::system_clock 生成 UTC 毫秒。
/// 推荐新代码统一走这个入口，方便上层精确判断错误来源。
void set_last_error_full(rflow_err_t code,
                         const std::string& msg,
                         const std::string& origin = {});

/// 显式清空 last_error；上层一般不需要主动调用，因为下一次写入会覆盖。
void clear_last_error();

/// 读 thread-local 的当前快照（C++ 内部使用）。
struct LastErrorSnapshot {
    rflow_err_t code{RFLOW_OK};
    const char* message{""};
    const char* origin{""};
    uint64_t    timestamp_ms{0};
};
LastErrorSnapshot get_last_error_snapshot();

}  // namespace rflow

#endif  // __RFLOW_COMMON_BASE_LAST_ERROR_H__

#ifndef __RFLOW_COMMON_LOG_LAST_ERROR_H__
#define __RFLOW_COMMON_LOG_LAST_ERROR_H__

#include <cstdint>
#include <string>

#include "rflow/librflow_common.h"

namespace rflow {

void set_last_error(const std::string& msg);
void set_last_error(const char* msg);
void set_last_error(const std::string& msg, const char* origin);
void set_last_error(const char* msg, const char* origin);
void set_last_error(rflow_err_t code, const std::string& msg, const char* origin);
void set_last_error_full(rflow_err_t code,
                         const std::string& msg,
                         const std::string& origin = {});
void clear_last_error();

struct LastErrorSnapshot {
    rflow_err_t code{RFLOW_OK};
    const char* message{""};
    const char* origin{""};
    uint64_t    timestamp_ms{0};
};
LastErrorSnapshot get_last_error_snapshot();

}  // namespace rflow

#endif  // __RFLOW_COMMON_LOG_LAST_ERROR_H__

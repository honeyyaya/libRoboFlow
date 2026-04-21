/**
 * @file   last_error.cpp
 * @brief  thread-local 错误文本存储
**/

#include "robrt/librobrt_common.h"

#include <string>

namespace {

thread_local std::string g_last_error;

}  // namespace

namespace robrt {

void set_last_error(const std::string& msg) {
    g_last_error = msg;
}

void set_last_error(const char* msg) {
    g_last_error = msg ? msg : "";
}

}  // namespace robrt

extern "C" const char* librobrt_get_last_error(void) {
    return g_last_error.c_str();
}

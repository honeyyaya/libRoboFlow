/**
 * @file   logger.cpp
 * @brief  运行期 logger —— 持久化 log_level / callback / userdata
**/

#include "robrt/librobrt_common.h"

#include "../internal/logger.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace {

std::mutex                      g_mu;
std::atomic<robrt_log_level_t>  g_level{ROBRT_LOG_INFO};
librobrt_log_cb_fn              g_cb       = nullptr;
void*                           g_userdata = nullptr;

}  // namespace

namespace robrt {

void logger_apply(robrt_log_level_t level, librobrt_log_cb_fn cb, void* ud) {
    g_level.store(level, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(g_mu);
    g_cb       = cb;
    g_userdata = ud;
}

void logger_set_level(robrt_log_level_t level) {
    g_level.store(level, std::memory_order_relaxed);
}

void logger_set_callback(librobrt_log_cb_fn cb, void* ud) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_cb       = cb;
    g_userdata = ud;
}

void logf(robrt_log_level_t level, const char* fmt, ...) {
    if (level < g_level.load(std::memory_order_relaxed)) return;

    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    librobrt_log_cb_fn cb = nullptr;
    void* ud              = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        cb = g_cb;
        ud = g_userdata;
    }
    if (cb) {
        cb(level, buf, ud);
    } else {
        std::fprintf(stderr, "[robrt][%d] %s\n", level, buf);
    }
}

}  // namespace robrt

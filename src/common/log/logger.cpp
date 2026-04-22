/**
 * @file   logger.cpp
 * @brief  运行期 logger —— 持久化 log_level / callback / userdata
**/

#include "rflow/librflow_common.h"

#include "../internal/logger.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace {

std::mutex                      g_mu;
std::atomic<rflow_log_level_t>  g_level{RFLOW_LOG_INFO};
librflow_log_cb_fn              g_cb       = nullptr;
void*                           g_userdata = nullptr;

}  // namespace

namespace rflow {

void logger_apply(rflow_log_level_t level, librflow_log_cb_fn cb, void* ud) {
    g_level.store(level, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(g_mu);
    g_cb       = cb;
    g_userdata = ud;
}

void logger_set_level(rflow_log_level_t level) {
    g_level.store(level, std::memory_order_relaxed);
}

void logger_set_callback(librflow_log_cb_fn cb, void* ud) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_cb       = cb;
    g_userdata = ud;
}

void logf(rflow_log_level_t level, const char* fmt, ...) {
    if (level < g_level.load(std::memory_order_relaxed)) return;

    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    librflow_log_cb_fn cb = nullptr;
    void* ud              = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        cb = g_cb;
        ud = g_userdata;
    }
    if (cb) {
        cb(level, buf, ud);
    } else {
        std::fprintf(stderr, "[rflow][%d] %s\n", level, buf);
    }
}

}  // namespace rflow

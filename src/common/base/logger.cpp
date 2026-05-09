#include "rflow/librflow_common.h"

#include "common/base/logger.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace {

std::mutex g_logger_mutex;
std::atomic<rflow_log_level_t> g_log_level{RFLOW_LOG_INFO};
librflow_log_cb_fn g_log_callback = nullptr;
void* g_log_userdata = nullptr;

}  // namespace

namespace rflow {

void logger_apply(rflow_log_level_t level, librflow_log_cb_fn cb, void* ud) {
    g_log_level.store(level, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    g_log_callback = cb;
    g_log_userdata = ud;
}

void logger_set_level(rflow_log_level_t level) {
    g_log_level.store(level, std::memory_order_relaxed);
}

void logger_set_callback(librflow_log_cb_fn cb, void* ud) {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    g_log_callback = cb;
    g_log_userdata = ud;
}

void logf(rflow_log_level_t level, const char* fmt, ...) {
    if (level < g_log_level.load(std::memory_order_relaxed)) return;

    char log_buffer[1024];
    va_list var_args;
    va_start(var_args, fmt);
    vsnprintf(log_buffer, sizeof(log_buffer), fmt, var_args);
    va_end(var_args);

    librflow_log_cb_fn callback = nullptr;
    void* callback_userdata = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_logger_mutex);
        callback = g_log_callback;
        callback_userdata = g_log_userdata;
    }
    if (callback) {
        callback(level, log_buffer, callback_userdata);
    } else {
        std::fprintf(stderr, "[rflow][%d] %s\n", level, log_buffer);
    }
}

}  // namespace rflow

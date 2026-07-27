#include "rflow/librflow_common.h"

#include "log/last_error.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace {

struct State {
    rflow_err_t code{RFLOW_OK};
    std::string message;
    std::string origin;
    uint64_t    timestamp_ms{0};
};

thread_local State t_state;

uint64_t NowUtcMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

}  // namespace

namespace rflow {

void set_last_error(const std::string& msg) {
    t_state.message = msg;
    if (t_state.code == RFLOW_OK) {
        t_state.code = RFLOW_ERR_FAIL;
    }
    t_state.timestamp_ms = NowUtcMs();
}

void set_last_error(const char* msg) {
    set_last_error(std::string(msg ? msg : ""));
}

void set_last_error(const std::string& msg, const char* origin) {
    t_state.message = msg;
    t_state.origin = origin ? origin : "";
    if (t_state.code == RFLOW_OK) {
        t_state.code = RFLOW_ERR_FAIL;
    }
    t_state.timestamp_ms = NowUtcMs();
}

void set_last_error(const char* msg, const char* origin) {
    set_last_error(std::string(msg ? msg : ""), origin);
}

void set_last_error(rflow_err_t code, const std::string& msg, const char* origin) {
    set_last_error_full(code, msg, origin ? std::string(origin) : std::string{});
}

void set_last_error_full(rflow_err_t code,
                         const std::string& msg,
                         const std::string& origin) {
    t_state.code = code;
    t_state.message = msg;
    t_state.origin = origin;
    t_state.timestamp_ms = NowUtcMs();
}

void clear_last_error() {
    t_state.code = RFLOW_OK;
    t_state.message.clear();
    t_state.origin.clear();
    t_state.timestamp_ms = 0;
}

LastErrorSnapshot get_last_error_snapshot() {
    LastErrorSnapshot s;
    s.code = t_state.code;
    s.message = t_state.message.c_str();
    s.origin = t_state.origin.c_str();
    s.timestamp_ms = t_state.timestamp_ms;
    return s;
}

}  // namespace rflow

extern "C" const char* librflow_get_last_error(void) {
    return t_state.message.c_str();
}

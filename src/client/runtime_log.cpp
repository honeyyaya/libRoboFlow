#include "robrt/Client/librobrt_client_api.h"

#include "common/internal/logger.h"

extern "C" {

robrt_err_t librobrt_log_set_level(robrt_log_level_t level) {
    robrt::logger_set_level(level);
    return ROBRT_OK;
}

robrt_err_t librobrt_log_set_callback(librobrt_log_cb_fn cb, void* userdata) {
    robrt::logger_set_callback(cb, userdata);
    return ROBRT_OK;
}

}  // extern "C"

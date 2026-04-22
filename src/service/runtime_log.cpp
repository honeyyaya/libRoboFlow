#include "rflow/Service/librflow_service_api.h"

#include "common/internal/logger.h"

extern "C" {

rflow_err_t librflow_svc_log_set_level(rflow_log_level_t level) {
    rflow::logger_set_level(level);
    return RFLOW_OK;
}

rflow_err_t librflow_svc_log_set_callback(librflow_log_cb_fn cb, void* userdata) {
    rflow::logger_set_callback(cb, userdata);
    return RFLOW_OK;
}

}  // extern "C"

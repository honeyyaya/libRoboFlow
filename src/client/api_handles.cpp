#include "rflow/Client/librflow_client_api.h"

#include "internal/handles.h"

#include "public/object_access_api.h"
#include "public/logger_api.h"

extern "C" {

librflow_connect_info_t librflow_connect_info_create(void) {
    return rflow::common::abi::CreateMagicObject<librflow_connect_info_s>(
        rflow::client::kMagicConnectInfo);
}

void librflow_connect_info_destroy(librflow_connect_info_t info) {
    rflow::common::abi::DestroyMagicObject(info, rflow::client::kMagicConnectInfo);
}

rflow_err_t librflow_connect_info_set_device_id(librflow_connect_info_t info, const char* id) {
    RFLOW_CHECK_HANDLE(info, rflow::client::kMagicConnectInfo);
    info->device_id = id ? id : "";
    return RFLOW_OK;
}

rflow_err_t librflow_connect_info_set_device_secret(librflow_connect_info_t info, const char* sec) {
    RFLOW_CHECK_HANDLE(info, rflow::client::kMagicConnectInfo);
    info->device_secret = sec ? sec : "";
    return RFLOW_OK;
}

librflow_connect_cb_t librflow_connect_cb_create(void) {
    return rflow::common::abi::CreateMagicObject<librflow_connect_cb_s>(
        rflow::client::kMagicConnectCb);
}

void librflow_connect_cb_destroy(librflow_connect_cb_t cb) {
    rflow::common::abi::DestroyMagicObject(cb, rflow::client::kMagicConnectCb);
}

#define SET_CB(field, type)                                                          \
    rflow_err_t librflow_connect_cb_set_##field(librflow_connect_cb_t cb, type f) { \
        RFLOW_SET_FIELD(cb, rflow::client::kMagicConnectCb, field, f);              \
    }

SET_CB(on_state, librflow_on_connect_state_fn)
SET_CB(on_notice, librflow_on_notice_fn)
SET_CB(on_service_req, librflow_on_service_req_fn)

#undef SET_CB

rflow_err_t librflow_connect_cb_set_userdata(librflow_connect_cb_t cb, void* ud) {
    RFLOW_SET_FIELD(cb, rflow::client::kMagicConnectCb, userdata, ud);
}

rflow_err_t librflow_log_set_level(rflow_log_level_t level) {
    rflow::logger_set_level(level);
    return RFLOW_OK;
}

rflow_err_t librflow_log_set_callback(librflow_log_cb_fn cb, void* userdata) {
    rflow::logger_set_callback(cb, userdata);
    return RFLOW_OK;
}

}  // extern "C"

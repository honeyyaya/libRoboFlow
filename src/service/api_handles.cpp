#include "rflow/Service/librflow_service_api.h"

#include "internal/handles.h"

#include "common/base/abi_string_copy.h"
#include "common/public/object_access_api.h"
#include "common/public/logger_api.h"

extern "C" {

librflow_svc_connect_info_t librflow_svc_connect_info_create(void) {
    return rflow::common::abi::CreateMagicObject<librflow_svc_connect_info_s>(
        rflow::service::kMagicConnectInfo);
}

void librflow_svc_connect_info_destroy(librflow_svc_connect_info_t info) {
    rflow::common::abi::DestroyMagicObject(info, rflow::service::kMagicConnectInfo);
}

#define SVC_SI_SET(field)                                                                 \
    rflow_err_t librflow_svc_connect_info_set_##field(librflow_svc_connect_info_t info,   \
                                                      const char* v) {                     \
        RFLOW_SET_FIELD(info, rflow::service::kMagicConnectInfo, field, (v ? v : ""));   \
    }

SVC_SI_SET(device_id)
SVC_SI_SET(device_secret)
SVC_SI_SET(product_key)

#undef SVC_SI_SET

rflow_err_t librflow_svc_connect_info_set_vendor_id(librflow_svc_connect_info_t info, const char* v) {
    RFLOW_SET_FIELD(info, rflow::service::kMagicConnectInfo, vendor_id, (v ? v : ""));
}

#define SVC_CI_GET(field)                                                                 \
    rflow_err_t librflow_svc_connect_info_get_##field(librflow_svc_connect_info_t info,   \
                                                       char* buf, uint32_t buf_len,        \
                                                       uint32_t* out_needed) {             \
        RFLOW_CHECK_HANDLE(info, rflow::service::kMagicConnectInfo);                      \
        return rflow::common::base::CopyOutString(info->field, buf, buf_len, out_needed); \
    }

SVC_CI_GET(device_id)
SVC_CI_GET(device_secret)
SVC_CI_GET(product_key)
SVC_CI_GET(vendor_id)

#undef SVC_CI_GET

librflow_svc_connect_cb_t librflow_svc_connect_cb_create(void) {
    return rflow::common::abi::CreateMagicObject<librflow_svc_connect_cb_s>(
        rflow::service::kMagicConnectCb);
}

void librflow_svc_connect_cb_destroy(librflow_svc_connect_cb_t cb) {
    rflow::common::abi::DestroyMagicObject(cb, rflow::service::kMagicConnectCb);
}

#define SVC_CB_SET(field, type)                                                         \
    rflow_err_t librflow_svc_connect_cb_set_##field(librflow_svc_connect_cb_t cb, type fn) { \
        RFLOW_SET_FIELD(cb, rflow::service::kMagicConnectCb, field, fn);                \
    }

SVC_CB_SET(on_state, librflow_svc_on_connect_state_fn)
SVC_CB_SET(on_bind_state, librflow_svc_on_bind_state_fn)
SVC_CB_SET(on_notice, librflow_svc_on_notice_fn)
SVC_CB_SET(on_service_req, librflow_svc_on_service_req_fn)
SVC_CB_SET(on_pull_request, librflow_svc_on_pull_request_fn)
SVC_CB_SET(on_pull_release, librflow_svc_on_pull_release_fn)

#undef SVC_CB_SET

rflow_err_t librflow_svc_connect_cb_set_userdata(librflow_svc_connect_cb_t cb, void* ud) {
    RFLOW_SET_FIELD(cb, rflow::service::kMagicConnectCb, userdata, ud);
}

rflow_err_t librflow_svc_log_set_level(rflow_log_level_t level) {
    rflow::logger_set_level(level);
    return RFLOW_OK;
}

rflow_err_t librflow_svc_log_set_callback(librflow_log_cb_fn cb, void* userdata) {
    rflow::logger_set_callback(cb, userdata);
    return RFLOW_OK;
}

rflow_err_t librflow_svc_license_info_get_expire_time(librflow_svc_license_info_t info,
                                                       uint64_t* out_expire_sec) {
    RFLOW_CHECK_HANDLE(info, rflow::service::kMagicLicenseInfo);
    if (!out_expire_sec) return RFLOW_ERR_PARAM;
    if (!info->loaded) return RFLOW_ERR_NOT_FOUND;
    *out_expire_sec = info->expire_time_sec;
    return RFLOW_OK;
}

rflow_err_t librflow_svc_license_info_get_vendor_id(librflow_svc_license_info_t info,
                                                     char* buf,
                                                     uint32_t buf_len,
                                                     uint32_t* out_needed) {
    RFLOW_CHECK_HANDLE(info, rflow::service::kMagicLicenseInfo);
    return rflow::common::base::CopyOutString(info->vendor_id, buf, buf_len, out_needed);
}

rflow_err_t librflow_svc_license_info_get_product_key(librflow_svc_license_info_t info,
                                                       char* buf,
                                                       uint32_t buf_len,
                                                       uint32_t* out_needed) {
    RFLOW_CHECK_HANDLE(info, rflow::service::kMagicLicenseInfo);
    return rflow::common::base::CopyOutString(info->product_key, buf, buf_len, out_needed);
}

void librflow_svc_license_info_destroy(librflow_svc_license_info_t info) {
    rflow::common::abi::DestroyMagicObject(info, rflow::service::kMagicLicenseInfo);
}

}  // extern "C"

#include "robrt/Service/librobrt_service_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librobrt_svc_connect_info_t librobrt_svc_connect_info_create(void) {
    auto* p = new (std::nothrow) librobrt_svc_connect_info_s();
    if (!p) return nullptr;
    p->magic = robrt::service::kMagicConnectInfo;
    return p;
}

void librobrt_svc_connect_info_destroy(librobrt_svc_connect_info_t info) {
    if (!info || info->magic != robrt::service::kMagicConnectInfo) return;
    info->magic = 0;
    delete info;
}

#define SVC_SI_SET(field)                                                                     \
    robrt_err_t librobrt_svc_connect_info_set_##field(librobrt_svc_connect_info_t info,       \
                                                      const char* v) {                         \
        ROBRT_CHECK_HANDLE(info, robrt::service::kMagicConnectInfo);                          \
        info->field = v ? v : "";                                                             \
        return ROBRT_OK;                                                                       \
    }

SVC_SI_SET(device_id)
SVC_SI_SET(device_secret)
SVC_SI_SET(product_key)

#undef SVC_SI_SET

robrt_err_t librobrt_svc_connect_info_set_vendor_id(librobrt_svc_connect_info_t info,
                                                     const char* v) {
    ROBRT_CHECK_HANDLE(info, robrt::service::kMagicConnectInfo);
    info->vendor_id = v ? v : "";
    return ROBRT_OK;
}

}  // extern "C"

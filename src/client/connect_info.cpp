#include "robrt/Client/librobrt_client_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librobrt_connect_info_t librobrt_connect_info_create(void) {
    auto* p = new (std::nothrow) librobrt_connect_info_s();
    if (!p) return nullptr;
    p->magic = robrt::client::kMagicConnectInfo;
    return p;
}

void librobrt_connect_info_destroy(librobrt_connect_info_t info) {
    if (!info || info->magic != robrt::client::kMagicConnectInfo) return;
    info->magic = 0;
    delete info;
}

robrt_err_t librobrt_connect_info_set_device_id(librobrt_connect_info_t info,
                                                 const char* id) {
    ROBRT_CHECK_HANDLE(info, robrt::client::kMagicConnectInfo);
    info->device_id = id ? id : "";
    return ROBRT_OK;
}

robrt_err_t librobrt_connect_info_set_device_secret(librobrt_connect_info_t info,
                                                     const char* sec) {
    ROBRT_CHECK_HANDLE(info, robrt::client::kMagicConnectInfo);
    info->device_secret = sec ? sec : "";
    return ROBRT_OK;
}

}  // extern "C"

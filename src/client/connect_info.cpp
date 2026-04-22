#include "rflow/Client/librflow_client_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librflow_connect_info_t librflow_connect_info_create(void) {
    auto* p = new (std::nothrow) librflow_connect_info_s();
    if (!p) return nullptr;
    p->magic = rflow::client::kMagicConnectInfo;
    return p;
}

void librflow_connect_info_destroy(librflow_connect_info_t info) {
    if (!info || info->magic != rflow::client::kMagicConnectInfo) return;
    info->magic = 0;
    delete info;
}

rflow_err_t librflow_connect_info_set_device_id(librflow_connect_info_t info,
                                                 const char* id) {
    RFLOW_CHECK_HANDLE(info, rflow::client::kMagicConnectInfo);
    info->device_id = id ? id : "";
    return RFLOW_OK;
}

rflow_err_t librflow_connect_info_set_device_secret(librflow_connect_info_t info,
                                                     const char* sec) {
    RFLOW_CHECK_HANDLE(info, rflow::client::kMagicConnectInfo);
    info->device_secret = sec ? sec : "";
    return RFLOW_OK;
}

}  // extern "C"

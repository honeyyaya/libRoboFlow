#include "robrt/Client/librobrt_client_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librobrt_connect_cb_t librobrt_connect_cb_create(void) {
    auto* p = new (std::nothrow) librobrt_connect_cb_s();
    if (!p) return nullptr;
    p->magic = robrt::client::kMagicConnectCb;
    return p;
}

void librobrt_connect_cb_destroy(librobrt_connect_cb_t cb) {
    if (!cb || cb->magic != robrt::client::kMagicConnectCb) return;
    cb->magic = 0;
    delete cb;
}

#define SET_CB(field, type)                                                       \
    robrt_err_t librobrt_connect_cb_set_##field(librobrt_connect_cb_t cb, type f) { \
        ROBRT_CHECK_HANDLE(cb, robrt::client::kMagicConnectCb);                   \
        cb->field = f;                                                            \
        return ROBRT_OK;                                                          \
    }

SET_CB(on_state,        librobrt_on_connect_state_fn)
SET_CB(on_notice,       librobrt_on_notice_fn)
SET_CB(on_service_req,  librobrt_on_service_req_fn)

#undef SET_CB

robrt_err_t librobrt_connect_cb_set_userdata(librobrt_connect_cb_t cb, void* ud) {
    ROBRT_CHECK_HANDLE(cb, robrt::client::kMagicConnectCb);
    cb->userdata = ud;
    return ROBRT_OK;
}

}  // extern "C"

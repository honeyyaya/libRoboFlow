#include "rflow/Client/librflow_client_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librflow_connect_cb_t librflow_connect_cb_create(void) {
    auto* p = new (std::nothrow) librflow_connect_cb_s();
    if (!p) return nullptr;
    p->magic = rflow::client::kMagicConnectCb;
    return p;
}

void librflow_connect_cb_destroy(librflow_connect_cb_t cb) {
    if (!cb || cb->magic != rflow::client::kMagicConnectCb) return;
    cb->magic = 0;
    delete cb;
}

#define SET_CB(field, type)                                                       \
    rflow_err_t librflow_connect_cb_set_##field(librflow_connect_cb_t cb, type f) { \
        RFLOW_CHECK_HANDLE(cb, rflow::client::kMagicConnectCb);                   \
        cb->field = f;                                                            \
        return RFLOW_OK;                                                          \
    }

SET_CB(on_state,        librflow_on_connect_state_fn)
SET_CB(on_notice,       librflow_on_notice_fn)
SET_CB(on_service_req,  librflow_on_service_req_fn)

#undef SET_CB

rflow_err_t librflow_connect_cb_set_userdata(librflow_connect_cb_t cb, void* ud) {
    RFLOW_CHECK_HANDLE(cb, rflow::client::kMagicConnectCb);
    cb->userdata = ud;
    return RFLOW_OK;
}

}  // extern "C"

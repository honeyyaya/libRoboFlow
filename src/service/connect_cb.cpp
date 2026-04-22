#include "rflow/Service/librflow_service_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librflow_svc_connect_cb_t librflow_svc_connect_cb_create(void) {
    auto* p = new (std::nothrow) librflow_svc_connect_cb_s();
    if (!p) return nullptr;
    p->magic = rflow::service::kMagicConnectCb;
    return p;
}

void librflow_svc_connect_cb_destroy(librflow_svc_connect_cb_t cb) {
    if (!cb || cb->magic != rflow::service::kMagicConnectCb) return;
    cb->magic = 0;
    delete cb;
}

#define SVC_CB_SET(field, type)                                                        \
    rflow_err_t librflow_svc_connect_cb_set_##field(librflow_svc_connect_cb_t cb,       \
                                                    type fn) {                          \
        RFLOW_CHECK_HANDLE(cb, rflow::service::kMagicConnectCb);                        \
        cb->field = fn;                                                                 \
        return RFLOW_OK;                                                                \
    }

SVC_CB_SET(on_state,        librflow_svc_on_connect_state_fn)
SVC_CB_SET(on_bind_state,   librflow_svc_on_bind_state_fn)
SVC_CB_SET(on_notice,       librflow_svc_on_notice_fn)
SVC_CB_SET(on_service_req,  librflow_svc_on_service_req_fn)
SVC_CB_SET(on_pull_request, librflow_svc_on_pull_request_fn)
SVC_CB_SET(on_pull_release, librflow_svc_on_pull_release_fn)

#undef SVC_CB_SET

rflow_err_t librflow_svc_connect_cb_set_userdata(librflow_svc_connect_cb_t cb, void* ud) {
    RFLOW_CHECK_HANDLE(cb, rflow::service::kMagicConnectCb);
    cb->userdata = ud;
    return RFLOW_OK;
}

}  // extern "C"

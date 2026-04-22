#include "rflow/Service/librflow_service_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librflow_svc_stream_cb_t librflow_svc_stream_cb_create(void) {
    auto* p = new (std::nothrow) librflow_svc_stream_cb_s();
    if (!p) return nullptr;
    p->magic = rflow::service::kMagicStreamCb;
    return p;
}

void librflow_svc_stream_cb_destroy(librflow_svc_stream_cb_t cb) {
    if (!cb || cb->magic != rflow::service::kMagicStreamCb) return;
    cb->magic = 0;
    delete cb;
}

rflow_err_t librflow_svc_stream_cb_set_on_state(librflow_svc_stream_cb_t cb,
                                                 librflow_svc_on_stream_state_fn fn) {
    RFLOW_CHECK_HANDLE(cb, rflow::service::kMagicStreamCb);
    cb->on_state = fn;
    return RFLOW_OK;
}

rflow_err_t librflow_svc_stream_cb_set_on_encoded_video(librflow_svc_stream_cb_t cb,
                                                         librflow_svc_on_encoded_video_fn fn) {
    RFLOW_CHECK_HANDLE(cb, rflow::service::kMagicStreamCb);
    cb->on_encoded_video = fn;
    return RFLOW_OK;
}

rflow_err_t librflow_svc_stream_cb_set_on_stream_stats(librflow_svc_stream_cb_t cb,
                                                        librflow_svc_on_stream_stats_fn fn) {
    RFLOW_CHECK_HANDLE(cb, rflow::service::kMagicStreamCb);
    cb->on_stream_stats = fn;
    return RFLOW_OK;
}

rflow_err_t librflow_svc_stream_cb_set_userdata(librflow_svc_stream_cb_t cb, void* ud) {
    RFLOW_CHECK_HANDLE(cb, rflow::service::kMagicStreamCb);
    cb->userdata = ud;
    return RFLOW_OK;
}

}  // extern "C"

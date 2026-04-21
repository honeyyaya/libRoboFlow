#include "robrt/Service/librobrt_service_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librobrt_svc_stream_cb_t librobrt_svc_stream_cb_create(void) {
    auto* p = new (std::nothrow) librobrt_svc_stream_cb_s();
    if (!p) return nullptr;
    p->magic = robrt::service::kMagicStreamCb;
    return p;
}

void librobrt_svc_stream_cb_destroy(librobrt_svc_stream_cb_t cb) {
    if (!cb || cb->magic != robrt::service::kMagicStreamCb) return;
    cb->magic = 0;
    delete cb;
}

robrt_err_t librobrt_svc_stream_cb_set_on_state(librobrt_svc_stream_cb_t cb,
                                                 librobrt_svc_on_stream_state_fn fn) {
    ROBRT_CHECK_HANDLE(cb, robrt::service::kMagicStreamCb);
    cb->on_state = fn;
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_stream_cb_set_on_encoded_video(librobrt_svc_stream_cb_t cb,
                                                         librobrt_svc_on_encoded_video_fn fn) {
    ROBRT_CHECK_HANDLE(cb, robrt::service::kMagicStreamCb);
    cb->on_encoded_video = fn;
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_stream_cb_set_on_stream_stats(librobrt_svc_stream_cb_t cb,
                                                        librobrt_svc_on_stream_stats_fn fn) {
    ROBRT_CHECK_HANDLE(cb, robrt::service::kMagicStreamCb);
    cb->on_stream_stats = fn;
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_stream_cb_set_userdata(librobrt_svc_stream_cb_t cb, void* ud) {
    ROBRT_CHECK_HANDLE(cb, robrt::service::kMagicStreamCb);
    cb->userdata = ud;
    return ROBRT_OK;
}

}  // extern "C"

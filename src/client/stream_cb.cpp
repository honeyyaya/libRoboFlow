#include "robrt/Client/librobrt_client_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librobrt_stream_cb_t librobrt_stream_cb_create(void) {
    auto* p = new (std::nothrow) librobrt_stream_cb_s();
    if (!p) return nullptr;
    p->magic = robrt::client::kMagicStreamCb;
    return p;
}

void librobrt_stream_cb_destroy(librobrt_stream_cb_t cb) {
    if (!cb || cb->magic != robrt::client::kMagicStreamCb) return;
    cb->magic = 0;
    delete cb;
}

robrt_err_t librobrt_stream_cb_set_on_state(librobrt_stream_cb_t cb, librobrt_on_stream_state_fn fn) {
    ROBRT_CHECK_HANDLE(cb, robrt::client::kMagicStreamCb);
    cb->on_state = fn;
    return ROBRT_OK;
}

robrt_err_t librobrt_stream_cb_set_on_video(librobrt_stream_cb_t cb, librobrt_on_video_frame_fn fn) {
    ROBRT_CHECK_HANDLE(cb, robrt::client::kMagicStreamCb);
    cb->on_video = fn;
    return ROBRT_OK;
}

robrt_err_t librobrt_stream_cb_set_on_stream_stats(librobrt_stream_cb_t cb, librobrt_on_stream_stats_fn fn) {
    ROBRT_CHECK_HANDLE(cb, robrt::client::kMagicStreamCb);
    cb->on_stream_stats = fn;
    return ROBRT_OK;
}

robrt_err_t librobrt_stream_cb_set_userdata(librobrt_stream_cb_t cb, void* ud) {
    ROBRT_CHECK_HANDLE(cb, robrt::client::kMagicStreamCb);
    cb->userdata = ud;
    return ROBRT_OK;
}

}  // extern "C"

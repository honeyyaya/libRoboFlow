#include "rflow/Client/librflow_client_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librflow_stream_cb_t librflow_stream_cb_create(void) {
    auto* p = new (std::nothrow) librflow_stream_cb_s();
    if (!p) return nullptr;
    p->magic = rflow::client::kMagicStreamCb;
    return p;
}

void librflow_stream_cb_destroy(librflow_stream_cb_t cb) {
    if (!cb || cb->magic != rflow::client::kMagicStreamCb) return;
    cb->magic = 0;
    delete cb;
}

rflow_err_t librflow_stream_cb_set_on_state(librflow_stream_cb_t cb, librflow_on_stream_state_fn fn) {
    RFLOW_CHECK_HANDLE(cb, rflow::client::kMagicStreamCb);
    cb->on_state = fn;
    return RFLOW_OK;
}

rflow_err_t librflow_stream_cb_set_on_video(librflow_stream_cb_t cb, librflow_on_video_frame_fn fn) {
    RFLOW_CHECK_HANDLE(cb, rflow::client::kMagicStreamCb);
    cb->on_video = fn;
    return RFLOW_OK;
}

rflow_err_t librflow_stream_cb_set_on_stream_stats(librflow_stream_cb_t cb, librflow_on_stream_stats_fn fn) {
    RFLOW_CHECK_HANDLE(cb, rflow::client::kMagicStreamCb);
    cb->on_stream_stats = fn;
    return RFLOW_OK;
}

rflow_err_t librflow_stream_cb_set_userdata(librflow_stream_cb_t cb, void* ud) {
    RFLOW_CHECK_HANDLE(cb, rflow::client::kMagicStreamCb);
    cb->userdata = ud;
    return RFLOW_OK;
}

}  // extern "C"

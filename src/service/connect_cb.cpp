#include "robrt/Service/librobrt_service_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librobrt_svc_connect_cb_t librobrt_svc_connect_cb_create(void) {
    auto* p = new (std::nothrow) librobrt_svc_connect_cb_s();
    if (!p) return nullptr;
    p->magic = robrt::service::kMagicConnectCb;
    return p;
}

void librobrt_svc_connect_cb_destroy(librobrt_svc_connect_cb_t cb) {
    if (!cb || cb->magic != robrt::service::kMagicConnectCb) return;
    cb->magic = 0;
    delete cb;
}

#define SVC_CB_SET(field, type)                                                        \
    robrt_err_t librobrt_svc_connect_cb_set_##field(librobrt_svc_connect_cb_t cb,       \
                                                    type fn) {                          \
        ROBRT_CHECK_HANDLE(cb, robrt::service::kMagicConnectCb);                        \
        cb->field = fn;                                                                 \
        return ROBRT_OK;                                                                \
    }

SVC_CB_SET(on_state,        librobrt_svc_on_connect_state_fn)
SVC_CB_SET(on_bind_state,   librobrt_svc_on_bind_state_fn)
SVC_CB_SET(on_notice,       librobrt_svc_on_notice_fn)
SVC_CB_SET(on_service_req,  librobrt_svc_on_service_req_fn)
SVC_CB_SET(on_pull_request, librobrt_svc_on_pull_request_fn)
SVC_CB_SET(on_pull_release, librobrt_svc_on_pull_release_fn)
SVC_CB_SET(on_talk_start,   librobrt_svc_on_talk_start_fn)
SVC_CB_SET(on_talk_stop,    librobrt_svc_on_talk_stop_fn)
SVC_CB_SET(on_talk_audio,   librobrt_svc_on_talk_audio_fn)
SVC_CB_SET(on_talk_video,   librobrt_svc_on_talk_video_fn)
SVC_CB_SET(on_stream_stats, librobrt_svc_on_stream_stats_fn)

#undef SVC_CB_SET

robrt_err_t librobrt_svc_connect_cb_set_userdata(librobrt_svc_connect_cb_t cb, void* ud) {
    ROBRT_CHECK_HANDLE(cb, robrt::service::kMagicConnectCb);
    cb->userdata = ud;
    return ROBRT_OK;
}

}  // extern "C"

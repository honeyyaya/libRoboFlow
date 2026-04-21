#include "robrt/Service/librobrt_service_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librobrt_svc_push_frame_t librobrt_svc_push_frame_create(void) {
    auto* p = new (std::nothrow) librobrt_svc_push_frame_s();
    if (!p) return nullptr;
    p->magic = robrt::service::kMagicPushFrame;
    p->flush = true;
    return p;
}

void librobrt_svc_push_frame_destroy(librobrt_svc_push_frame_t f) {
    if (!f || f->magic != robrt::service::kMagicPushFrame) return;
    f->magic = 0;
    delete f;
}

#define SVC_PF_CHECK(f) ROBRT_CHECK_HANDLE(f, robrt::service::kMagicPushFrame)

robrt_err_t librobrt_svc_push_frame_set_codec (librobrt_svc_push_frame_t f, robrt_codec_t c) {
    SVC_PF_CHECK(f); f->codec = c; return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_type  (librobrt_svc_push_frame_t f, robrt_frame_type_t t) {
    SVC_PF_CHECK(f); f->type = t; return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_data  (librobrt_svc_push_frame_t f,
                                                const void* data, uint32_t size) {
    SVC_PF_CHECK(f);
    if ((data == nullptr) != (size == 0)) return ROBRT_ERR_PARAM;
    const auto* p = static_cast<const uint8_t*>(data);
    f->data.assign(p, p + size);
    return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_size  (librobrt_svc_push_frame_t f, uint32_t w, uint32_t h) {
    SVC_PF_CHECK(f); f->width = w; f->height = h; return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_pts_ms(librobrt_svc_push_frame_t f, uint64_t v) {
    SVC_PF_CHECK(f); f->pts_ms = v; return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_utc_ms(librobrt_svc_push_frame_t f, uint64_t v) {
    SVC_PF_CHECK(f); f->utc_ms = v; return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_seq   (librobrt_svc_push_frame_t f, uint32_t v) {
    SVC_PF_CHECK(f); f->seq = v; return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_flush (librobrt_svc_push_frame_t f, bool v) {
    SVC_PF_CHECK(f); f->flush = v; return ROBRT_OK;
}
robrt_err_t librobrt_svc_push_frame_set_offset(librobrt_svc_push_frame_t f, uint32_t v) {
    SVC_PF_CHECK(f); f->offset = v; return ROBRT_OK;
}

#undef SVC_PF_CHECK

}  // extern "C"

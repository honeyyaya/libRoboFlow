#include "robrt/librobrt_common.h"

#include "../internal/frame_impl.h"

#include <cstddef>

extern "C" {

robrt_codec_t librobrt_video_frame_get_codec(librobrt_video_frame_t f) {
    if (!f || f->magic != robrt::kMagicVideoFrame) return ROBRT_CODEC_UNKNOWN;
    return f->codec;
}

robrt_frame_type_t librobrt_video_frame_get_type(librobrt_video_frame_t f) {
    if (!f || f->magic != robrt::kMagicVideoFrame) return ROBRT_FRAME_UNKNOWN;
    return f->type;
}

const uint8_t* librobrt_video_frame_get_data(librobrt_video_frame_t f) {
    if (!f || f->magic != robrt::kMagicVideoFrame) return nullptr;
    return f->payload.data();
}

uint32_t librobrt_video_frame_get_data_size(librobrt_video_frame_t f) {
    if (!f || f->magic != robrt::kMagicVideoFrame) return 0;
    return static_cast<uint32_t>(f->payload.size());
}

uint32_t librobrt_video_frame_get_width (librobrt_video_frame_t f) { return f ? f->width  : 0; }
uint32_t librobrt_video_frame_get_height(librobrt_video_frame_t f) { return f ? f->height : 0; }
uint64_t librobrt_video_frame_get_pts_ms(librobrt_video_frame_t f) { return f ? f->pts_ms : 0; }
uint64_t librobrt_video_frame_get_utc_ms(librobrt_video_frame_t f) { return f ? f->utc_ms : 0; }
uint32_t librobrt_video_frame_get_seq   (librobrt_video_frame_t f) { return f ? f->seq    : 0; }
int32_t  librobrt_video_frame_get_index (librobrt_video_frame_t f) { return f ? f->stream_index : -1; }

librobrt_video_frame_t librobrt_video_frame_retain(librobrt_video_frame_t f) {
    if (!f || f->magic != robrt::kMagicVideoFrame) return nullptr;
    const_cast<librobrt_video_frame_s*>(f)->refcount.fetch_add(1, std::memory_order_relaxed);
    return f;
}

void librobrt_video_frame_release(librobrt_video_frame_t f) {
    if (!f || f->magic != robrt::kMagicVideoFrame) return;
    auto* mf = const_cast<librobrt_video_frame_s*>(f);
    if (mf->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        mf->magic = 0;
        delete mf;
    }
}

}  // extern "C"

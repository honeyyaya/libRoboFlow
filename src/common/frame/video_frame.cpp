#include "rflow/librflow_common.h"

#include "../internal/frame_impl.h"

#include <cstddef>

extern "C" {

rflow_codec_t librflow_video_frame_get_codec(librflow_video_frame_t f) {
    if (!f || f->magic != rflow::kMagicVideoFrame) return RFLOW_CODEC_UNKNOWN;
    return f->codec;
}

rflow_frame_type_t librflow_video_frame_get_type(librflow_video_frame_t f) {
    if (!f || f->magic != rflow::kMagicVideoFrame) return RFLOW_FRAME_UNKNOWN;
    return f->type;
}

const uint8_t* librflow_video_frame_get_data(librflow_video_frame_t f) {
    if (!f || f->magic != rflow::kMagicVideoFrame) return nullptr;
    return f->payload.data();
}

uint32_t librflow_video_frame_get_data_size(librflow_video_frame_t f) {
    if (!f || f->magic != rflow::kMagicVideoFrame) return 0;
    return static_cast<uint32_t>(f->payload.size());
}

uint32_t librflow_video_frame_get_width (librflow_video_frame_t f) { return f ? f->width  : 0; }
uint32_t librflow_video_frame_get_height(librflow_video_frame_t f) { return f ? f->height : 0; }
uint64_t librflow_video_frame_get_pts_ms(librflow_video_frame_t f) { return f ? f->pts_ms : 0; }
uint64_t librflow_video_frame_get_utc_ms(librflow_video_frame_t f) { return f ? f->utc_ms : 0; }
uint32_t librflow_video_frame_get_seq   (librflow_video_frame_t f) { return f ? f->seq    : 0; }
int32_t  librflow_video_frame_get_index (librflow_video_frame_t f) { return f ? f->stream_index : -1; }

librflow_video_frame_t librflow_video_frame_retain(librflow_video_frame_t f) {
    if (!f || f->magic != rflow::kMagicVideoFrame) return nullptr;
    const_cast<librflow_video_frame_s*>(f)->refcount.fetch_add(1, std::memory_order_relaxed);
    return f;
}

void librflow_video_frame_release(librflow_video_frame_t f) {
    if (!f || f->magic != rflow::kMagicVideoFrame) return;
    auto* mf = const_cast<librflow_video_frame_s*>(f);
    if (mf->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        mf->magic = 0;
        delete mf;
    }
}

}  // extern "C"

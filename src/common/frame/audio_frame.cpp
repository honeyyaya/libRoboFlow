#include "robrt/librobrt_common.h"

#include "../internal/frame_impl.h"

extern "C" {

robrt_audio_codec_t librobrt_audio_frame_get_codec(librobrt_audio_frame_t f) {
    if (!f || f->magic != robrt::kMagicAudioFrame) return ROBRT_AUDIO_UNKNOWN;
    return f->codec;
}

const uint8_t* librobrt_audio_frame_get_data(librobrt_audio_frame_t f) {
    if (!f || f->magic != robrt::kMagicAudioFrame) return nullptr;
    return f->payload.data();
}

uint32_t librobrt_audio_frame_get_data_size(librobrt_audio_frame_t f) {
    if (!f || f->magic != robrt::kMagicAudioFrame) return 0;
    return static_cast<uint32_t>(f->payload.size());
}

uint32_t librobrt_audio_frame_get_sample_rate(librobrt_audio_frame_t f) { return f ? f->sample_rate  : 0; }
uint32_t librobrt_audio_frame_get_channel    (librobrt_audio_frame_t f) { return f ? f->channel      : 0; }
uint32_t librobrt_audio_frame_get_sample_bit (librobrt_audio_frame_t f) { return f ? f->sample_bit   : 0; }
uint64_t librobrt_audio_frame_get_pts_ms     (librobrt_audio_frame_t f) { return f ? f->pts_ms       : 0; }
uint64_t librobrt_audio_frame_get_utc_ms     (librobrt_audio_frame_t f) { return f ? f->utc_ms       : 0; }
uint32_t librobrt_audio_frame_get_seq        (librobrt_audio_frame_t f) { return f ? f->seq          : 0; }
int32_t  librobrt_audio_frame_get_index      (librobrt_audio_frame_t f) { return f ? f->stream_index : -1; }

librobrt_audio_frame_t librobrt_audio_frame_retain(librobrt_audio_frame_t f) {
    if (!f || f->magic != robrt::kMagicAudioFrame) return nullptr;
    const_cast<librobrt_audio_frame_s*>(f)->refcount.fetch_add(1, std::memory_order_relaxed);
    return f;
}

void librobrt_audio_frame_release(librobrt_audio_frame_t f) {
    if (!f || f->magic != robrt::kMagicAudioFrame) return;
    auto* mf = const_cast<librobrt_audio_frame_s*>(f);
    if (mf->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        mf->magic = 0;
        delete mf;
    }
}

}  // extern "C"

#include "rflow/librflow_common.h"

#include "../internal/frame_impl.h"

#include <cstddef>
#include <cstring>

namespace {

bool IsValidFrame(librflow_video_frame_t f) {
    return f && f->magic == rflow::kMagicVideoFrame;
}

bool EnsurePayloadMaterialized(const librflow_video_frame_s* f) {
    if (!f) return false;

    std::lock_guard<std::mutex> lk(f->payload_mu);
    if (!f->payload.empty()) return true;
    if (f->plane_count == 0) return false;

    switch (f->codec) {
        case RFLOW_CODEC_I420: {
            if (f->plane_count < 3) return false;
            size_t total = 0;
            for (uint32_t i = 0; i < 3; ++i) {
                total += static_cast<size_t>(f->plane_widths[i]) *
                         static_cast<size_t>(f->plane_heights[i]);
            }
            f->payload.resize(total);
            uint8_t* dst = f->payload.data();
            for (uint32_t i = 0; i < 3; ++i) {
                const uint32_t row_bytes = f->plane_widths[i];
                const uint32_t rows = f->plane_heights[i];
                for (uint32_t row = 0; row < rows; ++row) {
                    std::memcpy(dst,
                                f->plane_data[i] +
                                    static_cast<size_t>(row) * f->plane_strides[i],
                                row_bytes);
                    dst += row_bytes;
                }
            }
            return true;
        }
        case RFLOW_CODEC_NV12: {
            if (f->plane_count < 2) return false;
            const size_t y_size =
                static_cast<size_t>(f->plane_widths[0]) * f->plane_heights[0];
            const size_t uv_size =
                static_cast<size_t>(f->plane_widths[1]) * f->plane_heights[1];
            f->payload.resize(y_size + uv_size);
            uint8_t* dst = f->payload.data();
            for (uint32_t row = 0; row < f->plane_heights[0]; ++row) {
                std::memcpy(dst,
                            f->plane_data[0] +
                                static_cast<size_t>(row) * f->plane_strides[0],
                            f->plane_widths[0]);
                dst += f->plane_widths[0];
            }
            for (uint32_t row = 0; row < f->plane_heights[1]; ++row) {
                std::memcpy(dst,
                            f->plane_data[1] +
                                static_cast<size_t>(row) * f->plane_strides[1],
                            f->plane_widths[1]);
                dst += f->plane_widths[1];
            }
            return true;
        }
        default:
            return false;
    }
}

}  // namespace

extern "C" {

rflow_video_frame_backend_t librflow_video_frame_get_backend(librflow_video_frame_t f) {
    return IsValidFrame(f) ? f->backend : RFLOW_VIDEO_FRAME_BACKEND_UNKNOWN;
}

rflow_native_handle_type_t librflow_video_frame_get_native_handle_type(librflow_video_frame_t f) {
    return IsValidFrame(f) ? f->native_handle_type : RFLOW_NATIVE_HANDLE_NONE;
}

uint32_t librflow_video_frame_get_oes_texture_id(librflow_video_frame_t f) {
    if (!IsValidFrame(f) || f->native_handle_type != RFLOW_NATIVE_HANDLE_ANDROID_OES_TEXTURE) {
        return 0;
    }
    return f->oes_texture_id;
}

void* librflow_video_frame_get_android_hardware_buffer(librflow_video_frame_t f) {
    if (!IsValidFrame(f) || f->native_handle_type != RFLOW_NATIVE_HANDLE_ANDROID_HARDWARE_BUFFER) {
        return nullptr;
    }
    return f->android_hardware_buffer;
}

int32_t librflow_video_frame_get_sync_fence_fd(librflow_video_frame_t f) {
    if (!IsValidFrame(f)) return -1;
    return f->sync_fence_fd;
}

rflow_err_t librflow_video_frame_get_gl_prepare_callback(librflow_video_frame_t f,
                                                         librflow_gl_prepare_fn *out_fn,
                                                         void **out_userdata) {
    if (!IsValidFrame(f) || !out_fn || !out_userdata) return RFLOW_ERR_PARAM;
    *out_fn = nullptr;
    *out_userdata = nullptr;

    if (!f->gl_prepare_fn) return RFLOW_ERR_NOT_SUPPORT;

    *out_fn = f->gl_prepare_fn;
    *out_userdata = f->gl_prepare_userdata;
    return RFLOW_OK;
}

rflow_err_t librflow_video_frame_acquire_for_sampling(librflow_video_frame_t f) {
    if (!IsValidFrame(f)) return RFLOW_ERR_PARAM;
    if (!f->sampling_acquire_fn) return RFLOW_ERR_NOT_SUPPORT;
    return f->sampling_acquire_fn(f->sampling_userdata);
}

void librflow_video_frame_release_after_sampling(librflow_video_frame_t f) {
    if (!IsValidFrame(f) || !f->sampling_release_fn) return;
    f->sampling_release_fn(f->sampling_userdata);
}

rflow_codec_t librflow_video_frame_get_codec(librflow_video_frame_t f) {
    if (!IsValidFrame(f)) return RFLOW_CODEC_UNKNOWN;
    return f->codec;
}

rflow_frame_type_t librflow_video_frame_get_type(librflow_video_frame_t f) {
    if (!IsValidFrame(f)) return RFLOW_FRAME_UNKNOWN;
    return f->type;
}

uint32_t librflow_video_frame_get_plane_count(librflow_video_frame_t f) {
    return IsValidFrame(f) ? f->plane_count : 0;
}

const uint8_t* librflow_video_frame_get_plane_data(librflow_video_frame_t f, uint32_t plane_index) {
    if (!IsValidFrame(f) || plane_index >= f->plane_count) return nullptr;
    return f->plane_data[plane_index];
}

uint32_t librflow_video_frame_get_plane_stride(librflow_video_frame_t f, uint32_t plane_index) {
    if (!IsValidFrame(f) || plane_index >= f->plane_count) return 0;
    return f->plane_strides[plane_index];
}

uint32_t librflow_video_frame_get_plane_width(librflow_video_frame_t f, uint32_t plane_index) {
    if (!IsValidFrame(f) || plane_index >= f->plane_count) return 0;
    return f->plane_widths[plane_index];
}

uint32_t librflow_video_frame_get_plane_height(librflow_video_frame_t f, uint32_t plane_index) {
    if (!IsValidFrame(f) || plane_index >= f->plane_count) return 0;
    return f->plane_heights[plane_index];
}

const uint8_t* librflow_video_frame_get_data(librflow_video_frame_t f) {
    if (!IsValidFrame(f) || !EnsurePayloadMaterialized(f)) return nullptr;
    return f->payload.data();
}

uint32_t librflow_video_frame_get_data_size(librflow_video_frame_t f) {
    if (!IsValidFrame(f) || !EnsurePayloadMaterialized(f)) return 0;
    return static_cast<uint32_t>(f->payload.size());
}

uint32_t librflow_video_frame_get_width (librflow_video_frame_t f) { return IsValidFrame(f) ? f->width  : 0; }
uint32_t librflow_video_frame_get_height(librflow_video_frame_t f) { return IsValidFrame(f) ? f->height : 0; }
uint64_t librflow_video_frame_get_pts_ms(librflow_video_frame_t f) { return IsValidFrame(f) ? f->pts_ms : 0; }
uint64_t librflow_video_frame_get_utc_ms(librflow_video_frame_t f) { return IsValidFrame(f) ? f->utc_ms : 0; }
uint32_t librflow_video_frame_get_seq   (librflow_video_frame_t f) { return IsValidFrame(f) ? f->seq    : 0; }
int32_t  librflow_video_frame_get_index (librflow_video_frame_t f) { return IsValidFrame(f) ? f->stream_index : -1; }

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

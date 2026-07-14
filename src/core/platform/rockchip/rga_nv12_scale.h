#ifndef RFLOW_RGA_NV12_SCALE_H_
#define RFLOW_RGA_NV12_SCALE_H_

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "mpp_buffer.h"

namespace rflow::rtc::hw::rockchip_mpp {

struct RgaNv12CropScaleParams {
    int src_fd{-1};
    size_t src_buf_size{0};
    int src_width{0};
    int src_height{0};
    int src_hor_stride{0};
    int src_ver_stride{0};
    uint32_t src_mpp_fmt{0};
    int offset_x{0};
    int offset_y{0};
    int crop_width{0};
    int crop_height{0};
    int scaled_width{0};
    int scaled_height{0};
    int dst_fd{-1};
    size_t dst_buf_size{0};
    int dst_hor_stride{0};
    int dst_ver_stride{0};
};

/// NV12/NV21 DRM dma-buf crop + scale via RGA improcess. Requires RFLOW_HAVE_LIBRGA.
bool RgaNv12CropScaleDmabuf(const RgaNv12CropScaleParams& params);

int Nv12HorStrideAlign(int width);
int Nv12VerStrideAlign(int height);
size_t Nv12BufferSizeBytes(int hor_stride, int ver_stride);

/// Pool of MPP INTERNAL DRM buffers for RGA scale output (weak-network adaptation).
class MppDrmScaleBufferPool {
 public:
    static MppDrmScaleBufferPool& Instance();

    MppBuffer Acquire(int scaled_width, int scaled_height, int* out_hor_stride, int* out_ver_stride,
                      size_t* out_buf_bytes);
    void Release(MppBuffer buf);

 private:
    MppDrmScaleBufferPool();
    ~MppDrmScaleBufferPool();

    MppDrmScaleBufferPool(const MppDrmScaleBufferPool&) = delete;
    MppDrmScaleBufferPool& operator=(const MppDrmScaleBufferPool&) = delete;

    struct Slot {
        MppBuffer buf{nullptr};
        size_t capacity{0};
        int hor_stride{0};
        int ver_stride{0};
        bool in_use{false};
    };

    bool EnsureGroupLocked();
    MppBuffer AllocNewLocked(int hor_stride, int ver_stride, size_t capacity);

    mutable std::mutex mu_;
    void* group_{nullptr};
    int max_slots_{8};
    std::vector<Slot> slots_;
};

}  // namespace rflow::rtc::hw::rockchip_mpp

#endif  // RFLOW_RGA_NV12_SCALE_H_

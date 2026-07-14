#include "platform/rockchip/rga_dmabuf_sync.h"

#include <errno.h>
#include <sys/ioctl.h>

#include <linux/dma-buf.h>

namespace rflow::rtc::hw::rockchip_mpp {

void DmabufSyncStartRead(int dmabuf_fd) {
    if (dmabuf_fd < 0) {
        return;
    }
    struct dma_buf_sync sync {};
    sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ;
    (void)ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync);
}

void DmabufSyncEndRead(int dmabuf_fd) {
    if (dmabuf_fd < 0) {
        return;
    }
    struct dma_buf_sync sync {};
    sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
    (void)ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync);
}

void DmabufSyncStartWrite(int dmabuf_fd) {
    if (dmabuf_fd < 0) {
        return;
    }
    struct dma_buf_sync sync {};
    sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE;
    (void)ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync);
}

void DmabufSyncEndWrite(int dmabuf_fd) {
    if (dmabuf_fd < 0) {
        return;
    }
    struct dma_buf_sync sync {};
    sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
    (void)ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync);
}

}  // namespace rflow::rtc::hw::rockchip_mpp

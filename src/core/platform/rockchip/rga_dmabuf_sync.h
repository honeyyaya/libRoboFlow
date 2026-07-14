#ifndef RFLOW_RGA_DMABUF_SYNC_H_
#define RFLOW_RGA_DMABUF_SYNC_H_

namespace rflow::rtc::hw::rockchip_mpp {

void DmabufSyncStartRead(int dmabuf_fd);
void DmabufSyncEndRead(int dmabuf_fd);
void DmabufSyncStartWrite(int dmabuf_fd);
void DmabufSyncEndWrite(int dmabuf_fd);

}  // namespace rflow::rtc::hw::rockchip_mpp

#endif  // RFLOW_RGA_DMABUF_SYNC_H_

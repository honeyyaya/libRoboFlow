#ifndef RFLOW_PLATFORM_ROCKCHIP_MPP_HARDWARE_PROBE_H_
#define RFLOW_PLATFORM_ROCKCHIP_MPP_HARDWARE_PROBE_H_

namespace rflow::rtc::hw::rockchip_mpp {

/// 在已打开 MJPEG 硬件解码会话（与采集路径相同的 RkMppMjpegDecoder::Init）的前提下，
/// 再尝试创建 H.264 硬件编码会话（mpp_init ENC + AVC）。两者同时存活则返回 true。
///
/// 用于判断当前 BSP/驱动是否支持「双 MPP」协同；与仅顺序各 Init 一次相比，更接近真实推流场景。
bool ProbeRockchipConcurrentMjpegDecAndH264Enc();

}  // namespace rflow::rtc::hw::rockchip_mpp

#endif  // RFLOW_PLATFORM_ROCKCHIP_MPP_HARDWARE_PROBE_H_

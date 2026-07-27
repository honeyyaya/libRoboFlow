#ifndef __RFLOW_CORE_HW_FRAME_WEBRTC_FRAME_BRIDGE_H__
#define __RFLOW_CORE_HW_FRAME_WEBRTC_FRAME_BRIDGE_H__

#include "hw/frame/encoded_packet.h"
#include "hw/frame/video_frame.h"

#include "api/scoped_refptr.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"

namespace webrtc {
class EncodedImage;
}

namespace rflow::rtc::hw::rockchip_mpp {
class MppNativeDecFrameBuffer;
}

namespace rflow::hw {

/// 从 WebRTC VideoFrameBuffer 提取 MPP native 缓冲（含 kNative 路径）。
rflow::rtc::hw::rockchip_mpp::MppNativeDecFrameBuffer* TryGetMppNativeFromWebrtcBuffer(
    const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& buffer);

/// 若 buffer 为 MPP native 帧，写入进入 OnFrame 的时间戳（供延迟追踪）。
void StampNativeFrameEnterTime(const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& buffer,
                               int64_t t_us);

webrtc::VideoFrame ToWebrtcVideoFrame(const IVideoFrame& frame);

std::shared_ptr<IVideoFrame> FromWebrtcVideoFrame(const webrtc::VideoFrame& frame);

std::shared_ptr<IEncodedPacket> EncodedPacketFromWebrtcImage(const webrtc::EncodedImage& image,
                                                              HwCodecId codec);

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_FRAME_WEBRTC_FRAME_BRIDGE_H__

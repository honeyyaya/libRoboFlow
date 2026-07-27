#ifndef __RFLOW_CORE_HW_FRAME_FRAME_BRIDGE_H__
#define __RFLOW_CORE_HW_FRAME_FRAME_BRIDGE_H__

#include "hw/frame/video_frame.h"

struct librflow_video_frame_s;

namespace rflow::hw {

/// 填充已有 librflow_video_frame_s（不修改 struct 布局）。
bool ExportToAbi(const IVideoFrame& frame, librflow_video_frame_s* out);

/// 从 CPU 平面 ABI 帧导入；dmabuf/native 路径由平台 adapter 扩展。
std::shared_ptr<IVideoFrame> ImportFromAbi(const librflow_video_frame_s* in);

}  // namespace rflow::hw

#endif  // __RFLOW_CORE_HW_FRAME_FRAME_BRIDGE_H__

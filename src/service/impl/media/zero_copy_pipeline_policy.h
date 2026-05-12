/**
 * @file   zero_copy_pipeline_policy.h
 * @brief  把 MJPEG → MPP/H264 零拷贝管线的运行期开关收敛到一处。
 *
 * 历史背景：camera_video_track_source.cpp 中曾分散决定
 * 编码前是否走 dma_buf / RGA / native_zero_copy。它们都属于"零拷贝管线"这个领域概念，
 * 行为相互耦合（例如 ext_dma 与 RGA 二选一，且 native_zero_copy 是这俩任一启用时的优选路径）。
 *
 * 本模块把策略求值集中到一个轻量结构体，让 camera 主路径（OnDirectV4l2Buffer）只读
 * 字段，便于：
 *   1. 阅读与维护：所有 zero-copy 决策点集中在一处；
 *   2. 单元测试：可以直接验证 policy 字段；
 *   3. 后续策略升级：例如运行期一次性求值并锁定，或者按编码后端动态切换。
 */
#ifndef RFLOW_SERVICE_IMPL_MEDIA_ZERO_COPY_PIPELINE_POLICY_H_
#define RFLOW_SERVICE_IMPL_MEDIA_ZERO_COPY_PIPELINE_POLICY_H_

namespace rflow::service::impl::policy {

/// MJPEG 零拷贝管线决策三元组。所有字段都是"立即可用"的最终开关，
/// callsite 只需读对应字段而不再考虑编译条件。
struct MjpegZeroCopyPolicy {
    /// MJPEG → MPP 解码 → 编码器 直通（NativeDecFrameBuffer），跳过 NV12 中转。
    /// 受 RFLOW_MJPEG_ZERO_COPY_TO_ENC 控制；默认 ON。
    bool prefer_native_zero_copy_to_enc = true;

    /// 优先把 V4L2 mmap 出的 expbuf fd 透传给 MPP（VPU_FRAME_INFO_EXT_DMA_BUF），
    /// 失败时由解码层走 RGA 拷贝兜底。默认值由调用方传入的 v4l2_ext_dma_config_default 决定。
    bool use_v4l2_ext_dmabuf = false;

    /// 走 RGA 拷贝把 MJPEG bitstream 映射到 MPP 输入；与 use_v4l2_ext_dmabuf 互斥。
    /// 默认值由 mjpeg_rga_config_default 决定。
    /// 仅 RFLOW_HAVE_LIBRGA 编译时可能为 true。
    bool use_rga_to_mpp = false;
};

/// 求值一次 MJPEG zero-copy policy。
///
/// 行为约定：
///   - RFLOW_MJPEG_ZERO_COPY_TO_ENC 缺省 / 空串 / 非 '0' → prefer_native = true；
///     '0' → false。
///   - use_v4l2_ext_dmabuf 取 v4l2_ext_dma_config_default。
///   - use_rga_to_mpp 取 mjpeg_rga_config_default；不带 LIBRGA 时强制 false。
///
/// 调用方可在 hot path 内重新求值；该函数只做轻量字段合成。
MjpegZeroCopyPolicy EvaluateMjpegZeroCopyPolicy(bool v4l2_ext_dma_config_default,
                                                bool mjpeg_rga_config_default);

}  // namespace rflow::service::impl::policy

#endif  // RFLOW_SERVICE_IMPL_MEDIA_ZERO_COPY_PIPELINE_POLICY_H_

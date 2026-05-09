#include "media/zero_copy_pipeline_policy.h"

#include <cstdlib>

namespace rflow::service::impl::policy {

namespace {

bool ResolveBoolEnv(const char* name, bool default_value) {
    const char* e = std::getenv(name);
    if (!e || e[0] == '\0') {
        return default_value;
    }
    return e[0] != '0';
}

}  // namespace

MjpegZeroCopyPolicy EvaluateMjpegZeroCopyPolicy(bool v4l2_ext_dma_config_default,
                                                bool mjpeg_rga_config_default) {
    MjpegZeroCopyPolicy p;
    p.prefer_native_zero_copy_to_enc =
        ResolveBoolEnv("WEBRTC_MJPEG_ZERO_COPY_TO_ENC", true);
    p.use_v4l2_ext_dmabuf =
        ResolveBoolEnv("WEBRTC_MJPEG_V4L2_DMABUF", v4l2_ext_dma_config_default);
#if defined(RFLOW_HAVE_LIBRGA)
    p.use_rga_to_mpp =
        ResolveBoolEnv("WEBRTC_MJPEG_RGA_TO_MPP", mjpeg_rga_config_default);
#else
    (void)mjpeg_rga_config_default;
    p.use_rga_to_mpp = false;
#endif
    return p;
}

}  // namespace rflow::service::impl::policy

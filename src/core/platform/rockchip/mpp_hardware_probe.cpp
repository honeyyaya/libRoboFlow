#include "platform/rockchip/mpp_hardware_probe.h"

#include "platform/rockchip/mjpeg_decoder.h"

#include "public/log_tagged.h"
#include "mpp_err.h"
#include "rk_mpi.h"
#include "rk_type.h"

namespace rflow::rtc::hw::rockchip_mpp {

bool ProbeRockchipConcurrentMjpegDecAndH264Enc() {
    RkMppMjpegDecoder dec;
    if (!dec.Init()) {
        RFLOW_LOG_TAG_I("RkMppProbe",
                        "MJPEG+H264 concurrent probe: MJPEG decoder Init failed → no dual MPP");
        return false;
    }

    MppCtx  enc_ctx = nullptr;
    MppApi* enc_mpi = nullptr;
    bool enc_ok = false;
    const MPP_RET create_ret = mpp_create(&enc_ctx, &enc_mpi);
    if (create_ret != MPP_OK || !enc_ctx || !enc_mpi) {
        RFLOW_LOG_TAG_I("RkMppProbe",
                        "MJPEG+H264 concurrent probe: mpp_create(ENC) failed ret=%d → no dual MPP",
                        static_cast<int>(create_ret));
        dec.Close();
        return false;
    }

    const MPP_RET init_ret = mpp_init(enc_ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    enc_ok                   = init_ret == MPP_OK;
    mpp_destroy(enc_ctx);

    dec.Close();

    if (!enc_ok) {
        RFLOW_LOG_TAG_I(
            "RkMppProbe",
            "MJPEG+H264 concurrent probe: mpp_init(ENC, AVC) failed ret=%d while MJPEG dec open → no dual MPP",
            static_cast<int>(init_ret));
    } else {
        RFLOW_LOG_TAG_I("RkMppProbe", "MJPEG+H264 concurrent probe: OK → dual MPP allowed");
    }
    return enc_ok;
}

}  // namespace rflow::rtc::hw::rockchip_mpp

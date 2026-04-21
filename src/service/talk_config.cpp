#include "robrt/Service/librobrt_service_api.h"

#include "internal/handles.h"

#include <new>

extern "C" {

librobrt_svc_talk_config_t librobrt_svc_talk_config_create(void) {
    auto* p = new (std::nothrow) librobrt_svc_talk_config_s();
    if (!p) return nullptr;
    p->magic = robrt::service::kMagicTalkConfig;
    return p;
}

void librobrt_svc_talk_config_destroy(librobrt_svc_talk_config_t cfg) {
    if (!cfg || cfg->magic != robrt::service::kMagicTalkConfig) return;
    cfg->magic = 0;
    delete cfg;
}

robrt_err_t librobrt_svc_talk_config_set_audio(librobrt_svc_talk_config_t cfg,
                                                robrt_audio_codec_t codec,
                                                uint32_t sample_rate,
                                                uint32_t channel,
                                                uint32_t sample_bit) {
    ROBRT_CHECK_HANDLE(cfg, robrt::service::kMagicTalkConfig);
    cfg->audio_codec       = codec;
    cfg->audio_sample_rate = sample_rate;
    cfg->audio_channel     = channel;
    cfg->audio_sample_bit  = sample_bit;
    return ROBRT_OK;
}

robrt_err_t librobrt_svc_talk_config_set_video(librobrt_svc_talk_config_t cfg,
                                                robrt_codec_t codec,
                                                uint32_t max_bitrate_kbps) {
    ROBRT_CHECK_HANDLE(cfg, robrt::service::kMagicTalkConfig);
    cfg->video_codec            = codec;
    cfg->video_max_bitrate_kbps = max_bitrate_kbps;
    return ROBRT_OK;
}

}  // extern "C"

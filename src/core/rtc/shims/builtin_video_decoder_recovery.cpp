#include "rtc/shims/builtin_video_decoder_recovery.h"

#include "public/log_tagged.h"
#include "runtime/runtime_knobs.h"

#include "api/environment/environment.h"
#include "api/video/encoded_image.h"
#include "api/video/video_frame_type.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/sdp_video_format.h"
#include "modules/video_coding/include/video_error_codes.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace rflow::rtc::shims {
namespace {

namespace knob = rflow::core::runtime;

std::string LowerCopy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool RecoveryEnabledFromEnv() {
    return knob::ReadBool("RFLOW_BUILTIN_DECODE_RECOVERY");
}

int StallFramesFromEnv() {
    return knob::ReadInt("RFLOW_BUILTIN_DECODE_STALL_FRAMES");
}

int H264ProactiveResetFromEnv() {
    return knob::ReadInt("RFLOW_H264_DECODE_RESET_FRAME_COUNT");
}

class RecoveringVideoDecoder;

class OutputCountingCallback final : public webrtc::DecodedImageCallback {
 public:
    OutputCountingCallback(RecoveringVideoDecoder* owner, webrtc::DecodedImageCallback* user)
        : owner_(owner), user_(user) {}

    int32_t Decoded(webrtc::VideoFrame& decoded_image) override {
        NotifyOutput();
        return user_ ? user_->Decoded(decoded_image) : WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t Decoded(webrtc::VideoFrame& decoded_image, int64_t decode_time_ms) override {
        NotifyOutput();
        return user_ ? user_->Decoded(decoded_image, decode_time_ms) : WEBRTC_VIDEO_CODEC_OK;
    }

    void Decoded(webrtc::VideoFrame& decoded_image,
                 std::optional<int32_t> decode_time_ms,
                 std::optional<uint8_t> qp) override {
        NotifyOutput();
        if (user_) {
            user_->Decoded(decoded_image, decode_time_ms, qp);
        }
    }

    void SetUser(webrtc::DecodedImageCallback* user) { user_ = user; }

 private:
    void NotifyOutput();

    RecoveringVideoDecoder* owner_;
    webrtc::DecodedImageCallback* user_;
};

class RecoveringVideoDecoder final : public webrtc::VideoDecoder {
 public:
    RecoveringVideoDecoder(std::unique_ptr<webrtc::VideoDecoder> inner,
                            BuiltinDecoderRecoveryPolicy policy,
                            std::string codec_label)
        : inner_(std::move(inner)),
          policy_(policy),
          codec_label_(std::move(codec_label)),
          counting_cb_(this, nullptr) {}

    bool Configure(const Settings& settings) override {
        settings_ = settings;
        configured_ = inner_ && inner_->Configure(settings);
        return configured_;
    }

    int32_t Release() override {
        configured_ = false;
        decode_attempts_without_output_ = 0;
        return inner_ ? inner_->Release() : WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t RegisterDecodeCompleteCallback(webrtc::DecodedImageCallback* callback) override {
        user_callback_ = callback;
        counting_cb_.SetUser(user_callback_);
        return inner_ ? inner_->RegisterDecodeCompleteCallback(&counting_cb_)
                      : WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }

    int32_t Decode(const webrtc::EncodedImage& input_image,
                   bool missing_frames,
                   int64_t render_time_ms) override {
        if (!inner_) {
            return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
        }

        MaybeProactiveReset(input_image);

        int32_t ret = inner_->Decode(input_image, missing_frames, render_time_ms);
        ++decode_attempts_without_output_;

        if (ret != WEBRTC_VIDEO_CODEC_OK && ret != WEBRTC_VIDEO_CODEC_NO_OUTPUT) {
            if (TryRecover("decode_error", ret)) {
                ret = inner_->Decode(input_image, missing_frames, render_time_ms);
                ++decode_attempts_without_output_;
            }
        }

        if (policy_.enabled && decode_attempts_without_output_ >= policy_.stall_frames_without_output) {
            if (TryRecover("stall", ret)) {
                (void)inner_->Decode(input_image, missing_frames, render_time_ms);
                ++decode_attempts_without_output_;
                ret = WEBRTC_VIDEO_CODEC_OK;
            }
        }

        return ret;
    }

    DecoderInfo GetDecoderInfo() const override {
        DecoderInfo info;
        if (inner_) {
            info = inner_->GetDecoderInfo();
        }
        if (info.implementation_name.empty()) {
            info.implementation_name = codec_label_;
        }
        info.implementation_name += "+recovery";
        return info;
    }

    void OnDecodedOutput() {
        decode_attempts_without_output_ = 0;
        ++total_decoded_frames_;
    }

 private:
    void MaybeProactiveReset(const webrtc::EncodedImage& input_image) {
        if (!policy_.enabled || !configured_ || policy_.proactive_reset_after_frames <= 0) {
            return;
        }
        if (total_decoded_frames_ <
            static_cast<uint64_t>(policy_.proactive_reset_after_frames)) {
            return;
        }
        if (input_image.FrameType() != webrtc::VideoFrameType::kVideoFrameKey) {
            return;
        }
        TryRecover("proactive_keyframe_reset", WEBRTC_VIDEO_CODEC_OK);
    }

    bool TryRecover(const char* reason, int32_t last_ret) {
        if (!inner_ || !configured_) {
            return false;
        }
        ++recovery_count_;
        RFLOW_LOG_TAG_W(
            "BuiltinDecRecovery",
            "%s codec=%s recoveries=%u decoded_total=%llu last_ret=%d proactive_after=%d stall_frames=%d",
            reason, codec_label_.c_str(), recovery_count_,
            static_cast<unsigned long long>(total_decoded_frames_), last_ret,
            policy_.proactive_reset_after_frames, policy_.stall_frames_without_output);

        (void)inner_->Release();
        if (!inner_->Configure(settings_)) {
            RFLOW_LOG_TAG_E("BuiltinDecRecovery", "Configure after reset failed codec=%s",
                            codec_label_.c_str());
            configured_ = false;
            return false;
        }
        decode_attempts_without_output_ = 0;
        total_decoded_frames_ = 0;
        if (user_callback_) {
            (void)inner_->RegisterDecodeCompleteCallback(&counting_cb_);
        }
        return true;
    }

    std::unique_ptr<webrtc::VideoDecoder> inner_;
    BuiltinDecoderRecoveryPolicy policy_;
    std::string codec_label_;
    Settings settings_{};
    bool configured_{false};
    webrtc::DecodedImageCallback* user_callback_{nullptr};
    OutputCountingCallback counting_cb_;
    int decode_attempts_without_output_{0};
    uint64_t total_decoded_frames_{0};
    unsigned recovery_count_{0};
};

void OutputCountingCallback::NotifyOutput() {
    if (owner_) {
        owner_->OnDecodedOutput();
    }
}

bool ShouldWrapCodec(const std::string& codec_mime, const BuiltinDecoderRecoveryPolicy& policy) {
    if (!policy.enabled) {
        return false;
    }
    const std::string mime = LowerCopy(codec_mime);
    if (mime == "h264") {
        return true;
    }
    return policy.stall_frames_without_output > 0;
}

class RecoveringBuiltinVideoDecoderFactory final : public webrtc::VideoDecoderFactory {
 public:
    explicit RecoveringBuiltinVideoDecoderFactory(
        std::unique_ptr<webrtc::VideoDecoderFactory> builtin)
        : builtin_(std::move(builtin)) {}

    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
        return builtin_->GetSupportedFormats();
    }

    webrtc::VideoDecoderFactory::CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat& format,
                                                                bool reference_scaling) const override {
        return builtin_->QueryCodecSupport(format, reference_scaling);
    }

    std::unique_ptr<webrtc::VideoDecoder> Create(const webrtc::Environment& env,
                                                 const webrtc::SdpVideoFormat& format) override {
        auto inner = builtin_->Create(env, format);
        if (!inner) {
            return nullptr;
        }
        const BuiltinDecoderRecoveryPolicy policy = EvaluateBuiltinDecoderRecoveryPolicy(format.name);
        if (!ShouldWrapCodec(format.name, policy)) {
            return inner;
        }
        return std::make_unique<RecoveringVideoDecoder>(std::move(inner), policy, format.name);
    }

 private:
    std::unique_ptr<webrtc::VideoDecoderFactory> builtin_;
};

}  // namespace

BuiltinDecoderRecoveryPolicy RecoveryPolicyForFormat(const std::string& codec_mime) {
    BuiltinDecoderRecoveryPolicy p;
    p.enabled = RecoveryEnabledFromEnv();
    p.stall_frames_without_output = StallFramesFromEnv();
    p.proactive_reset_after_frames = 0;

    const std::string mime = LowerCopy(codec_mime);
    if (mime == "h264") {
        p.proactive_reset_after_frames = H264ProactiveResetFromEnv();
    }
    return p;
}

BuiltinDecoderRecoveryPolicy EvaluateBuiltinDecoderRecoveryPolicy(const std::string& codec_mime) {
    return RecoveryPolicyForFormat(codec_mime);
}

std::unique_ptr<webrtc::VideoDecoderFactory> CreateRecoveringBuiltinVideoDecoderFactory() {
    return std::make_unique<RecoveringBuiltinVideoDecoderFactory>(
        webrtc::CreateBuiltinVideoDecoderFactory());
}

}  // namespace rflow::rtc::shims

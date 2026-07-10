#include "rtc/shims/builtin_video_decoder_recovery.h"

#include <cstdlib>

#include "gtest/gtest.h"

namespace shims = rflow::rtc::shims;

namespace {

void ClearKnobs() {
    ::unsetenv("RFLOW_BUILTIN_DECODE_RECOVERY");
    ::unsetenv("RFLOW_FFMPEG_H264_DECODE_RECOVERY");
    ::unsetenv("RFLOW_BUILTIN_DECODE_STALL_FRAMES");
    ::unsetenv("RFLOW_FFMPEG_H264_DECODE_STALL_FRAMES");
    ::unsetenv("RFLOW_H264_DECODE_RESET_FRAME_COUNT");
    ::unsetenv("RFLOW_FFMPEG_H264_DECODE_RESET_FRAME_COUNT");
}

class BuiltinDecoderRecoveryPolicyTest : public ::testing::Test {
 protected:
    void SetUp() override { ClearKnobs(); }
    void TearDown() override { ClearKnobs(); }
};

}  // namespace

TEST_F(BuiltinDecoderRecoveryPolicyTest, H264HasProactiveReset) {
    const auto p = shims::RecoveryPolicyForFormat("H264");
    EXPECT_TRUE(p.enabled);
    EXPECT_EQ(p.proactive_reset_after_frames, 2800);
    EXPECT_EQ(p.stall_frames_without_output, 45);
}

TEST_F(BuiltinDecoderRecoveryPolicyTest, Vp8StallOnly) {
    const auto p = shims::RecoveryPolicyForFormat("VP8");
    EXPECT_TRUE(p.enabled);
    EXPECT_EQ(p.proactive_reset_after_frames, 0);
    EXPECT_EQ(p.stall_frames_without_output, 45);
}

TEST_F(BuiltinDecoderRecoveryPolicyTest, LegacyEnvAliasStillWorks) {
    ::setenv("RFLOW_FFMPEG_H264_DECODE_RECOVERY", "0", 1);
    ::setenv("RFLOW_FFMPEG_H264_DECODE_RESET_FRAME_COUNT", "1500", 1);
    const auto p = shims::RecoveryPolicyForFormat("h264");
    EXPECT_FALSE(p.enabled);
    ::setenv("RFLOW_FFMPEG_H264_DECODE_RECOVERY", "1", 1);
    ::setenv("RFLOW_H264_DECODE_RESET_FRAME_COUNT", "1500", 1);
    const auto p2 = shims::RecoveryPolicyForFormat("h264");
    EXPECT_TRUE(p2.enabled);
    EXPECT_EQ(p2.proactive_reset_after_frames, 1500);
}

TEST_F(BuiltinDecoderRecoveryPolicyTest, FactoryCreatesNonNull) {
    auto factory = shims::CreateRecoveringBuiltinVideoDecoderFactory();
    ASSERT_NE(factory, nullptr);
    EXPECT_FALSE(factory->GetSupportedFormats().empty());
}

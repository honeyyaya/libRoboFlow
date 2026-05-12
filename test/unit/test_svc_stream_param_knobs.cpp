// test_svc_stream_param_knobs — Service stream_param 常用项 ABI + ResolvePublisherStartup 映射

#include <gtest/gtest.h>

#include "internal/stream_startup_policy.h"
#include "rflow/Service/librflow_service_api.h"

TEST(SvcStreamParamDegradation, SetGetMaintainFramerate) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(librflow_svc_stream_param_set_degradation_preference(p, RFLOW_DEGRADATION_MAINTAIN_FRAMERATE),
              RFLOW_OK);
    rflow_degradation_preference_t v = RFLOW_DEGRADATION_BALANCED;
    EXPECT_EQ(librflow_svc_stream_param_get_degradation_preference(p, &v), RFLOW_OK);
    EXPECT_EQ(v, RFLOW_DEGRADATION_MAINTAIN_FRAMERATE);
    librflow_svc_stream_param_destroy(p);
}

TEST(SvcStreamParamDegradation, GetNotSetReturnsNotFound) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);
    rflow_degradation_preference_t v = RFLOW_DEGRADATION_BALANCED;
    EXPECT_EQ(librflow_svc_stream_param_get_degradation_preference(p, &v), RFLOW_ERR_NOT_FOUND);
    librflow_svc_stream_param_destroy(p);
}

TEST(SvcStreamParamH264, ProfileLevelRoundtrip) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(librflow_svc_stream_param_set_h264_profile(p, " main "), RFLOW_OK);
    EXPECT_EQ(librflow_svc_stream_param_set_h264_level(p, "4.2"), RFLOW_OK);

    char buf[256];
    uint32_t needed = 0;
    EXPECT_EQ(librflow_svc_stream_param_get_h264_profile(p, buf, sizeof(buf), &needed), RFLOW_OK);
    EXPECT_STREQ(buf, "main");
    EXPECT_EQ(librflow_svc_stream_param_get_h264_level(p, buf, sizeof(buf), nullptr), RFLOW_OK);
    EXPECT_STREQ(buf, "4.2");

    EXPECT_EQ(librflow_svc_stream_param_set_h264_profile(p, ""), RFLOW_ERR_PARAM);

    librflow_svc_stream_param_destroy(p);
}

TEST(SvcStreamParamIceAndNetwork, SetGetAndResolveLiterals) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);

    EXPECT_EQ(librflow_svc_stream_param_set_ice_prioritize_likely_pairs(p, false), RFLOW_OK);
    bool ice = true;
    EXPECT_EQ(librflow_svc_stream_param_get_ice_prioritize_likely_pairs(p, &ice), RFLOW_OK);
    EXPECT_FALSE(ice);

    EXPECT_EQ(librflow_svc_stream_param_set_video_network_priority(p, RFLOW_SVC_NETWORK_PRIORITY_VERY_LOW),
              RFLOW_OK);
    rflow_svc_network_priority_t np = RFLOW_SVC_NETWORK_PRIORITY_HIGH;
    EXPECT_EQ(librflow_svc_stream_param_get_video_network_priority(p, &np), RFLOW_OK);
    EXPECT_EQ(np, RFLOW_SVC_NETWORK_PRIORITY_VERY_LOW);

    librflow_svc_stream_s    stream{};
    stream.param           = *p;
    rflow::service::State state{};
    auto                  r = rflow::service::internal::ResolvePublisherStartup(stream, state, 0);
    ASSERT_TRUE(r.ice_prioritize_likely_pairs.has_value());
    EXPECT_FALSE(*r.ice_prioritize_likely_pairs);
    ASSERT_TRUE(r.video_network_priority.has_value());
    EXPECT_EQ(*r.video_network_priority, "very_low");

    librflow_svc_stream_param_destroy(p);
}

TEST(SvcStreamParamEncodeAndCapture, SetGetResolve) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);

    EXPECT_EQ(librflow_svc_stream_param_set_video_encoding_max_fps(p, 30), RFLOW_OK);
    uint32_t f = 0;
    EXPECT_EQ(librflow_svc_stream_param_get_video_encoding_max_fps(p, &f), RFLOW_OK);
    EXPECT_EQ(f, 30u);

    EXPECT_EQ(librflow_svc_stream_param_set_capture_warmup_sec(p, 2), RFLOW_OK);
    uint32_t w = 0;
    EXPECT_EQ(librflow_svc_stream_param_get_capture_warmup_sec(p, &w), RFLOW_OK);
    EXPECT_EQ(w, 2u);

    EXPECT_EQ(librflow_svc_stream_param_set_capture_gate(p, 5, 15), RFLOW_OK);
    uint32_t gate_min = 0, gate_max = 0;
    EXPECT_EQ(librflow_svc_stream_param_get_capture_gate(p, &gate_min, &gate_max), RFLOW_OK);
    EXPECT_EQ(gate_min, 5u);
    EXPECT_EQ(gate_max, 15u);

    EXPECT_EQ(librflow_svc_stream_param_set_gop(p, 120), RFLOW_OK);

    librflow_svc_stream_s    stream{};
    stream.param           = *p;
    rflow::service::State state{};
    auto                  r = rflow::service::internal::ResolvePublisherStartup(stream, state, 0);
    ASSERT_TRUE(r.video_encoding_max_framerate.has_value());
    EXPECT_EQ(*r.video_encoding_max_framerate, 30);
    ASSERT_TRUE(r.capture_warmup_sec.has_value());
    EXPECT_EQ(*r.capture_warmup_sec, 2);
    ASSERT_TRUE(r.capture_gate_min_frames.has_value());
    ASSERT_TRUE(r.capture_gate_max_wait_sec.has_value());
    EXPECT_EQ(*r.capture_gate_min_frames, 5);
    EXPECT_EQ(*r.capture_gate_max_wait_sec, 15);
    ASSERT_TRUE(r.keyframe_gop.has_value());
    EXPECT_EQ(*r.keyframe_gop, 120);

    librflow_svc_stream_param_destroy(p);
}

TEST(SvcStreamParamBitrateMode, SetGetRoundtrip) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);

    EXPECT_EQ(librflow_svc_stream_param_set_bitrate_mode(p, RFLOW_BITRATE_MODE_CBR), RFLOW_OK);
    rflow_bitrate_mode_t m = RFLOW_BITRATE_MODE_VBR;
    EXPECT_EQ(librflow_svc_stream_param_get_bitrate_mode(p, &m), RFLOW_OK);
    EXPECT_EQ(m, RFLOW_BITRATE_MODE_CBR);

    librflow_svc_stream_param_destroy(p);
}

TEST(SvcStreamParamBitrateMode, GetNotSetReturnsNotFound) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);
    rflow_bitrate_mode_t m = RFLOW_BITRATE_MODE_VBR;
    EXPECT_EQ(librflow_svc_stream_param_get_bitrate_mode(p, &m), RFLOW_ERR_NOT_FOUND);
    librflow_svc_stream_param_destroy(p);
}

TEST(SvcStreamParamBitrateMode, ResolveFromBitrateModeExplicit) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(librflow_svc_stream_param_set_bitrate_mode(p, RFLOW_BITRATE_MODE_VBR), RFLOW_OK);

    librflow_svc_stream_s stream{};
    stream.param                     = *p;
    rflow::service::State state{};
    auto                  r = rflow::service::internal::ResolvePublisherStartup(stream, state, 0);
    ASSERT_TRUE(r.bitrate_mode.has_value());
    EXPECT_EQ(*r.bitrate_mode, RFLOW_BITRATE_MODE_VBR);

    librflow_svc_stream_param_destroy(p);
}

TEST(SvcStreamParamBitrateMode, ResolveFromRcModeWhenBitrateModeUnset) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(librflow_svc_stream_param_set_rc_mode(p, RFLOW_RC_CBR), RFLOW_OK);

    librflow_svc_stream_s stream{};
    stream.param = *p;

    rflow::service::State state{};
    auto                  r = rflow::service::internal::ResolvePublisherStartup(stream, state, 0);
    ASSERT_TRUE(r.bitrate_mode.has_value());
    EXPECT_EQ(*r.bitrate_mode, RFLOW_BITRATE_MODE_CBR);

    librflow_svc_stream_param_destroy(p);
}

TEST(SvcStreamParamBitrateMode, InvalidModeRejected) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);
    const auto bad = static_cast<rflow_bitrate_mode_t>(9);
    EXPECT_EQ(librflow_svc_stream_param_set_bitrate_mode(p, bad), RFLOW_ERR_PARAM);
    librflow_svc_stream_param_destroy(p);
}

TEST(SvcStreamParamNetworkPriority, InvalidRejected) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);
    const auto bad = static_cast<rflow_svc_network_priority_t>(99);
    EXPECT_EQ(librflow_svc_stream_param_set_video_network_priority(p, bad), RFLOW_ERR_PARAM);
    librflow_svc_stream_param_destroy(p);
}

TEST(StreamStartupDegradationPreference, ApiThenResolvePropagatesStrings) {
    librflow_svc_stream_param_t param = librflow_svc_stream_param_create();
    ASSERT_NE(param, nullptr);
    EXPECT_EQ(librflow_svc_stream_param_set_degradation_preference(param, RFLOW_DEGRADATION_BALANCED),
              RFLOW_OK);

    librflow_svc_stream_s stream{};
    stream.param = *param;

    rflow::service::State state{};
    const auto resolved = rflow::service::internal::ResolvePublisherStartup(stream, state, 0);
    ASSERT_TRUE(resolved.degradation_pref_explicit.has_value());
    EXPECT_EQ(*resolved.degradation_pref_explicit, "balanced");

    librflow_svc_stream_param_destroy(param);
}

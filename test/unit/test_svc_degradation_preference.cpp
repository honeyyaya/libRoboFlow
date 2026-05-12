// test_svc_degradation_preference — Service stream_param degradation API + ResolvePublisherStartup 映射

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

TEST(SvcStreamParamDegradation, SetGetMaintainResolution) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(librflow_svc_stream_param_set_degradation_preference(p, RFLOW_DEGRADATION_MAINTAIN_RESOLUTION),
              RFLOW_OK);
    rflow_degradation_preference_t v = RFLOW_DEGRADATION_MAINTAIN_FRAMERATE;
    EXPECT_EQ(librflow_svc_stream_param_get_degradation_preference(p, &v), RFLOW_OK);
    EXPECT_EQ(v, RFLOW_DEGRADATION_MAINTAIN_RESOLUTION);
    librflow_svc_stream_param_destroy(p);
}

TEST(SvcStreamParamDegradation, SetGetBalanced) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(librflow_svc_stream_param_set_degradation_preference(p, RFLOW_DEGRADATION_BALANCED), RFLOW_OK);
    rflow_degradation_preference_t v = RFLOW_DEGRADATION_MAINTAIN_FRAMERATE;
    EXPECT_EQ(librflow_svc_stream_param_get_degradation_preference(p, &v), RFLOW_OK);
    EXPECT_EQ(v, RFLOW_DEGRADATION_BALANCED);
    librflow_svc_stream_param_destroy(p);
}

TEST(SvcStreamParamDegradation, GetNotSetReturnsNotFound) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);
    rflow_degradation_preference_t v = RFLOW_DEGRADATION_BALANCED;
    EXPECT_EQ(librflow_svc_stream_param_get_degradation_preference(p, &v), RFLOW_ERR_NOT_FOUND);
    librflow_svc_stream_param_destroy(p);
}

TEST(SvcStreamParamDegradation, InvalidEnumRejected) {
    librflow_svc_stream_param_t p = librflow_svc_stream_param_create();
    ASSERT_NE(p, nullptr);
    const auto invalid = static_cast<rflow_degradation_preference_t>(999);
    EXPECT_EQ(librflow_svc_stream_param_set_degradation_preference(p, invalid), RFLOW_ERR_PARAM);
    rflow_degradation_preference_t v = RFLOW_DEGRADATION_BALANCED;
    EXPECT_EQ(librflow_svc_stream_param_get_degradation_preference(p, &v), RFLOW_ERR_NOT_FOUND);
    librflow_svc_stream_param_destroy(p);
}

TEST(StreamStartupDegradationPreference, UnsetLeavesOptionalEmpty) {
    librflow_svc_stream_s           stream{};
    rflow::service::State           state{};
    const auto resolved = rflow::service::internal::ResolvePublisherStartup(stream, state, 0);
    EXPECT_FALSE(resolved.degradation_pref_explicit.has_value());
}

TEST(StreamStartupDegradationPreference, MaintainFramerateLiteral) {
    librflow_svc_stream_s stream{};
    stream.param.has_degradation_preference = true;
    stream.param.degradation_preference     = RFLOW_DEGRADATION_MAINTAIN_FRAMERATE;
    rflow::service::State state{};
    const auto resolved = rflow::service::internal::ResolvePublisherStartup(stream, state, 3);
    ASSERT_TRUE(resolved.degradation_pref_explicit.has_value());
    EXPECT_EQ(*resolved.degradation_pref_explicit, "maintain_framerate");
}

TEST(StreamStartupDegradationPreference, MaintainResolutionLiteral) {
    librflow_svc_stream_s stream{};
    stream.param.has_degradation_preference = true;
    stream.param.degradation_preference     = RFLOW_DEGRADATION_MAINTAIN_RESOLUTION;
    rflow::service::State state{};
    const auto resolved = rflow::service::internal::ResolvePublisherStartup(stream, state, 0);
    ASSERT_TRUE(resolved.degradation_pref_explicit.has_value());
    EXPECT_EQ(*resolved.degradation_pref_explicit, "maintain_resolution");
}

TEST(StreamStartupDegradationPreference, BalancedLiteral) {
    librflow_svc_stream_s stream{};
    stream.param.has_degradation_preference = true;
    stream.param.degradation_preference     = RFLOW_DEGRADATION_BALANCED;
    rflow::service::State state{};
    const auto resolved = rflow::service::internal::ResolvePublisherStartup(stream, state, 0);
    ASSERT_TRUE(resolved.degradation_pref_explicit.has_value());
    EXPECT_EQ(*resolved.degradation_pref_explicit, "balanced");
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

// Unit tests for media/zero_copy_pipeline_policy.h.
//
// 关注点：
//   - V4L2/RGA 路径取构造方传入的 default
//   - prefer_native_zero_copy_to_enc 默认 ON 的特殊语义
//   - LIBRGA 编译期开关：未编译时 use_rga_to_mpp 强制 false（编译条件由 cpp 处理，此处只能跑 host 上的实际行为）

#include "media/zero_copy_pipeline_policy.h"

#include <cstdlib>

#include "gtest/gtest.h"

namespace policy = rflow::service::impl::policy;

namespace {

// 单测在同一进程跑，需要每次清干净 env。
void ClearKnobs() {
    ::unsetenv("RFLOW_MJPEG_ZERO_COPY_TO_ENC");
}

class ZeroCopyPolicyTest : public ::testing::Test {
 protected:
    void SetUp() override { ClearKnobs(); }
    void TearDown() override { ClearKnobs(); }
};

}  // namespace

TEST_F(ZeroCopyPolicyTest, DefaultsRespectPreferNativeOn) {
    auto p = policy::EvaluateMjpegZeroCopyPolicy(false, false);
    EXPECT_TRUE(p.prefer_native_zero_copy_to_enc);
    EXPECT_FALSE(p.use_v4l2_ext_dmabuf);
    EXPECT_FALSE(p.use_rga_to_mpp);
}

TEST_F(ZeroCopyPolicyTest, DefaultsHonorCallerConfig) {
    auto p = policy::EvaluateMjpegZeroCopyPolicy(true, true);
    EXPECT_TRUE(p.prefer_native_zero_copy_to_enc);
    EXPECT_TRUE(p.use_v4l2_ext_dmabuf);
#if defined(RFLOW_HAVE_LIBRGA)
    EXPECT_TRUE(p.use_rga_to_mpp);
#else
    EXPECT_FALSE(p.use_rga_to_mpp);
#endif
}

TEST_F(ZeroCopyPolicyTest, EnvOverridesPreferNativeOff) {
    ::setenv("RFLOW_MJPEG_ZERO_COPY_TO_ENC", "0", 1);
    auto p = policy::EvaluateMjpegZeroCopyPolicy(false, false);
    EXPECT_FALSE(p.prefer_native_zero_copy_to_enc);
}

TEST_F(ZeroCopyPolicyTest, V4l2ExtDmabufFollowsCallerConfig) {
    auto p = policy::EvaluateMjpegZeroCopyPolicy(false, false);
    EXPECT_FALSE(p.use_v4l2_ext_dmabuf);

    p = policy::EvaluateMjpegZeroCopyPolicy(true, false);
    EXPECT_TRUE(p.use_v4l2_ext_dmabuf);
}

TEST_F(ZeroCopyPolicyTest, RgaPolicyRespectsCompileFlag) {
    auto p = policy::EvaluateMjpegZeroCopyPolicy(false, true);
#if defined(RFLOW_HAVE_LIBRGA)
    EXPECT_TRUE(p.use_rga_to_mpp);
#else
    EXPECT_FALSE(p.use_rga_to_mpp);
#endif
}

#include "media/capture_fps_pipeline_policy.h"

#include <cstdlib>

#include "gtest/gtest.h"

namespace policy = rflow::service::impl::policy;

namespace {

void ClearKnobs() {
    ::unsetenv("RFLOW_CAPTURE_FPS_PIPELINE");
    ::unsetenv("RFLOW_MJPEG_DECODE_QUEUE_MAX_WAIT_MS");
}

class CaptureFpsPipelinePolicyTest : public ::testing::Test {
 protected:
    void SetUp() override { ClearKnobs(); }
    void TearDown() override { ClearKnobs(); }
};

}  // namespace

TEST_F(CaptureFpsPipelinePolicyTest, Classify30FpsAsLowLatency) {
    EXPECT_EQ(policy::ClassifyCaptureFpsTier(30), policy::CaptureFpsTier::kLowLatency);
    EXPECT_EQ(policy::ClassifyCaptureFpsTier(44), policy::CaptureFpsTier::kLowLatency);
}

TEST_F(CaptureFpsPipelinePolicyTest, Classify60FpsAsHighFidelity) {
    EXPECT_EQ(policy::ClassifyCaptureFpsTier(60), policy::CaptureFpsTier::kHighFidelity);
    EXPECT_EQ(policy::ClassifyCaptureFpsTier(45), policy::CaptureFpsTier::kHighFidelity);
}

TEST_F(CaptureFpsPipelinePolicyTest, Apply60FpsPreset) {
    rflow::service::impl::PushStreamerBackendConfig backend;
    policy::ApplyCaptureFpsPipelineDefaults(backend, 60);
    EXPECT_FALSE(backend.mjpeg_queue_latest_only);
    EXPECT_EQ(backend.v4l2_buffer_count, 4);
    EXPECT_EQ(backend.mjpeg_queue_max, 3);
    EXPECT_EQ(backend.nv12_pool_slots, 6);
    EXPECT_EQ(backend.v4l2_poll_timeout_ms, 5);
}

TEST_F(CaptureFpsPipelinePolicyTest, Apply30FpsPreset) {
    rflow::service::impl::PushStreamerBackendConfig backend;
    policy::ApplyCaptureFpsPipelineDefaults(backend, 30);
    EXPECT_TRUE(backend.mjpeg_queue_latest_only);
    EXPECT_EQ(backend.v4l2_buffer_count, 2);
    EXPECT_EQ(backend.mjpeg_queue_max, 2);
    EXPECT_EQ(backend.nv12_pool_slots, 4);
}

TEST_F(CaptureFpsPipelinePolicyTest, StaleWaitAutoScalesWithFps) {
    EXPECT_EQ(policy::MjpegDecodeQueueMaxWaitMsForFps(60), 48);
    EXPECT_EQ(policy::MjpegDecodeQueueMaxWaitMsForFps(30), 25);
}

TEST_F(CaptureFpsPipelinePolicyTest, EnvOverridesStaleWait) {
    ::setenv("RFLOW_MJPEG_DECODE_QUEUE_MAX_WAIT_MS", "100", 1);
    EXPECT_EQ(policy::MjpegDecodeQueueMaxWaitMsForFps(60), 100);
}

TEST_F(CaptureFpsPipelinePolicyTest, ForceLowLatencyViaEnv) {
    ::setenv("RFLOW_CAPTURE_FPS_PIPELINE", "low_latency", 1);
    rflow::service::impl::PushStreamerBackendConfig backend;
    policy::ApplyCaptureFpsPipelineDefaults(backend, 60);
    EXPECT_TRUE(backend.mjpeg_queue_latest_only);
    EXPECT_EQ(backend.v4l2_buffer_count, 2);
}

TEST_F(CaptureFpsPipelinePolicyTest, ForceHighFidelityViaEnv) {
    ::setenv("RFLOW_CAPTURE_FPS_PIPELINE", "high_fidelity", 1);
    rflow::service::impl::PushStreamerBackendConfig backend;
    policy::ApplyCaptureFpsPipelineDefaults(backend, 30);
    EXPECT_FALSE(backend.mjpeg_queue_latest_only);
    EXPECT_EQ(backend.v4l2_buffer_count, 4);
}

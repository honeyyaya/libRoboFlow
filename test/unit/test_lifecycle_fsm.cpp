// test_lifecycle_fsm — 验证 ValidateConnectTransition 的状态机语义。

#include <gtest/gtest.h>

#include "base/lifecycle_fsm.h"

namespace {
enum class TestState { kUninit, kInited, kConnecting, kConnected };
}

using rflow::common::base::ValidateConnectTransition;

TEST(LifecycleFsmTest, RejectsUninit) {
    EXPECT_EQ(ValidateConnectTransition(TestState::kUninit, TestState::kUninit,
                                        TestState::kConnecting, TestState::kConnected),
              RFLOW_ERR_STATE);
}

TEST(LifecycleFsmTest, RejectsConnecting) {
    EXPECT_EQ(ValidateConnectTransition(TestState::kConnecting, TestState::kUninit,
                                        TestState::kConnecting, TestState::kConnected),
              RFLOW_ERR_STATE);
}

TEST(LifecycleFsmTest, RejectsConnected) {
    EXPECT_EQ(ValidateConnectTransition(TestState::kConnected, TestState::kUninit,
                                        TestState::kConnecting, TestState::kConnected),
              RFLOW_ERR_STATE);
}

TEST(LifecycleFsmTest, AcceptsInited) {
    EXPECT_EQ(ValidateConnectTransition(TestState::kInited, TestState::kUninit,
                                        TestState::kConnecting, TestState::kConnected),
              RFLOW_OK);
}

// test_env_reader — 验证环境变量解析工具的 fallback / 范围 clamp / 解析失败语义。
//
// 行为契约见 src/common/base/env_reader.h：
//   - ReadEnvIntInRange: 未设/空/解析失败 → fallback；越界 → clamp 到 [min, max]
//   - ReadEnvBool      : 1/y/Y/t/T → true；0/n/N/f/F → false；其它 → fallback
//   - ReadEnvSizeInRange: 同 IntInRange，但是 size_t 范围

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "base/env_reader.h"

namespace {

// 测试夹具：每个 case 可以 SetEnv / UnsetEnv，自动在 TearDown 清理已设置过的变量
class EnvReaderTest : public ::testing::Test {
protected:
    void SetUp() override { touched_.clear(); }
    void TearDown() override {
        for (const auto& name : touched_) {
            ::unsetenv(name.c_str());
        }
        touched_.clear();
    }

    void SetEnv(const std::string& name, const std::string& value) {
        ::setenv(name.c_str(), value.c_str(), 1);
        touched_.push_back(name);
    }
    void UnsetEnv(const std::string& name) {
        ::unsetenv(name.c_str());
        touched_.push_back(name);
    }

private:
    std::vector<std::string> touched_;
};

constexpr const char* kVar = "RFLOW_UTEST_ENV_READER_VAR";

}  // namespace

using rflow::common::base::ReadEnvBool;
using rflow::common::base::ReadEnvIntInRange;
using rflow::common::base::ReadEnvSizeInRange;

TEST_F(EnvReaderTest, IntUnsetReturnsFallback) {
    UnsetEnv(kVar);
    EXPECT_EQ(ReadEnvIntInRange(kVar, 42, 0, 100), 42);
}

TEST_F(EnvReaderTest, IntEmptyReturnsFallback) {
    SetEnv(kVar, "");
    EXPECT_EQ(ReadEnvIntInRange(kVar, 42, 0, 100), 42);
}

TEST_F(EnvReaderTest, IntValidParses) {
    SetEnv(kVar, "37");
    EXPECT_EQ(ReadEnvIntInRange(kVar, 0, 0, 100), 37);
}

TEST_F(EnvReaderTest, IntBelowMinClamps) {
    SetEnv(kVar, "-5");
    EXPECT_EQ(ReadEnvIntInRange(kVar, 0, 0, 100), 0);
}

TEST_F(EnvReaderTest, IntAboveMaxClamps) {
    SetEnv(kVar, "999");
    EXPECT_EQ(ReadEnvIntInRange(kVar, 0, 0, 100), 100);
}

TEST_F(EnvReaderTest, IntJunkSuffixReturnsFallback) {
    SetEnv(kVar, "12abc");
    EXPECT_EQ(ReadEnvIntInRange(kVar, 7, 0, 100), 7);
}

TEST_F(EnvReaderTest, IntPureGarbageReturnsFallback) {
    SetEnv(kVar, "hello");
    EXPECT_EQ(ReadEnvIntInRange(kVar, 7, 0, 100), 7);
}

TEST_F(EnvReaderTest, BoolUnsetReturnsFallback) {
    UnsetEnv(kVar);
    EXPECT_TRUE(ReadEnvBool(kVar, true));
    EXPECT_FALSE(ReadEnvBool(kVar, false));
}

TEST_F(EnvReaderTest, BoolTruthyForms) {
    for (const char* v : {"1", "y", "Y", "t", "T"}) {
        SetEnv(kVar, v);
        EXPECT_TRUE(ReadEnvBool(kVar, false)) << "v=" << v;
    }
}

TEST_F(EnvReaderTest, BoolFalsyForms) {
    for (const char* v : {"0", "n", "N", "f", "F"}) {
        SetEnv(kVar, v);
        EXPECT_FALSE(ReadEnvBool(kVar, true)) << "v=" << v;
    }
}

TEST_F(EnvReaderTest, BoolUnknownReturnsFallback) {
    SetEnv(kVar, "maybe");
    EXPECT_TRUE(ReadEnvBool(kVar, true));
    EXPECT_FALSE(ReadEnvBool(kVar, false));
}

TEST_F(EnvReaderTest, SizeUnsetReturnsFallback) {
    UnsetEnv(kVar);
    EXPECT_EQ(ReadEnvSizeInRange(kVar, 42u, 0u, 1024u), 42u);
}

TEST_F(EnvReaderTest, SizeBelowMinClampsToMin) {
    SetEnv(kVar, "5");
    EXPECT_EQ(ReadEnvSizeInRange(kVar, 0u, 10u, 1024u), 10u);
}

TEST_F(EnvReaderTest, SizeAboveMaxClampsToMax) {
    SetEnv(kVar, "9999");
    EXPECT_EQ(ReadEnvSizeInRange(kVar, 0u, 0u, 1024u), 1024u);
}

TEST_F(EnvReaderTest, SizeWithSuffixGarbageReturnsFallback) {
    SetEnv(kVar, "100xx");
    EXPECT_EQ(ReadEnvSizeInRange(kVar, 7u, 0u, 1024u), 7u);
}

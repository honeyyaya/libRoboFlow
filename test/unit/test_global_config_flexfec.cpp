// test_global_config_flexfec — GlobalConfig ABI：FlexFEC 显式开关

#include <gtest/gtest.h>

#include "rflow/librflow_common.h"

TEST(GlobalConfigFlexfec, DefaultNotExplicit) {
    librflow_global_config_t g = librflow_global_config_create();
    ASSERT_NE(g, nullptr);

    bool ex = true;
    bool en = false;
    EXPECT_EQ(librflow_global_config_get_enable_flexfec(g, &ex, &en), RFLOW_OK);
    EXPECT_FALSE(ex);

    librflow_global_config_destroy(g);
}

TEST(GlobalConfigFlexfec, SetGetReset) {
    librflow_global_config_t g = librflow_global_config_create();
    ASSERT_NE(g, nullptr);

    EXPECT_EQ(librflow_global_config_set_enable_flexfec(g, false), RFLOW_OK);
    bool ex = false;
    bool en = true;
    EXPECT_EQ(librflow_global_config_get_enable_flexfec(g, &ex, &en), RFLOW_OK);
    EXPECT_TRUE(ex);
    EXPECT_FALSE(en);

    EXPECT_EQ(librflow_global_config_set_enable_flexfec(g, true), RFLOW_OK);
    EXPECT_EQ(librflow_global_config_get_enable_flexfec(g, &ex, &en), RFLOW_OK);
    EXPECT_TRUE(ex);
    EXPECT_TRUE(en);

    librflow_global_config_reset_enable_flexfec(g);
    EXPECT_EQ(librflow_global_config_get_enable_flexfec(g, &ex, nullptr), RFLOW_OK);
    EXPECT_FALSE(ex);

    librflow_global_config_destroy(g);
}

TEST(GlobalConfigFlexfec, GetRejectNullExplicit) {
    librflow_global_config_t g = librflow_global_config_create();
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(librflow_global_config_get_enable_flexfec(g, nullptr, nullptr), RFLOW_ERR_PARAM);
    librflow_global_config_destroy(g);
}

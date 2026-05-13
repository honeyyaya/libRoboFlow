// test_global_config_flexfec — GlobalConfig ABI: FlexFEC tri-state via set_flexfec

#include <gtest/gtest.h>

#include "rflow/librflow_common.h"

TEST(GlobalConfigFlexfec, DefaultUsesEnvSemantics) {
    librflow_global_config_t g = librflow_global_config_create();
    ASSERT_NE(g, nullptr);
    rflow_global_flexfec_t m = RFLOW_GLOBAL_FLEXFEC_ON;
    EXPECT_EQ(librflow_global_config_get_flexfec(g, &m), RFLOW_OK);
    EXPECT_EQ(m, RFLOW_GLOBAL_FLEXFEC_DEFAULT);

    librflow_global_config_destroy(g);
}

TEST(GlobalConfigFlexfec, SetOnOffDefaultRoundtrip) {
    librflow_global_config_t g = librflow_global_config_create();
    ASSERT_NE(g, nullptr);

    EXPECT_EQ(librflow_global_config_set_flexfec(g, RFLOW_GLOBAL_FLEXFEC_OFF), RFLOW_OK);
    rflow_global_flexfec_t m{};
    EXPECT_EQ(librflow_global_config_get_flexfec(g, &m), RFLOW_OK);
    EXPECT_EQ(m, RFLOW_GLOBAL_FLEXFEC_OFF);

    EXPECT_EQ(librflow_global_config_set_flexfec(g, RFLOW_GLOBAL_FLEXFEC_ON), RFLOW_OK);
    EXPECT_EQ(librflow_global_config_get_flexfec(g, &m), RFLOW_OK);
    EXPECT_EQ(m, RFLOW_GLOBAL_FLEXFEC_ON);

    EXPECT_EQ(librflow_global_config_set_flexfec(g, RFLOW_GLOBAL_FLEXFEC_DEFAULT), RFLOW_OK);
    EXPECT_EQ(librflow_global_config_get_flexfec(g, &m), RFLOW_OK);
    EXPECT_EQ(m, RFLOW_GLOBAL_FLEXFEC_DEFAULT);

    librflow_global_config_destroy(g);
}

TEST(GlobalConfigFlexfec, SetRejectInvalidEnum) {
    librflow_global_config_t g = librflow_global_config_create();
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(librflow_global_config_set_flexfec(g,
                                               static_cast<rflow_global_flexfec_t>(99)),
              RFLOW_ERR_PARAM);
    librflow_global_config_destroy(g);
}

TEST(GlobalConfigFlexfec, GetRejectNullMode) {
    librflow_global_config_t g = librflow_global_config_create();
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(librflow_global_config_get_flexfec(g, nullptr), RFLOW_ERR_PARAM);
    librflow_global_config_destroy(g);
}

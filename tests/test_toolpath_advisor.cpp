#include <gtest/gtest.h>

#include "core/carve/toolpath_advisor.h"

using namespace dw::carve;

TEST(ToolpathAdvisor, DoesNotWarnForSmallPrograms)
{
    ToolpathConfig cfg;
    cfg.stepoverPreset = StepoverPreset::Fine;

    EXPECT_FALSE(adviseToolpathRuntime(cfg, 60.0f * 60.0f, 10000).has_value());
}

TEST(ToolpathAdvisor, SuggestsCoarserPresetForLongUltraFineProgram)
{
    ToolpathConfig cfg;
    cfg.stepoverPreset = StepoverPreset::UltraFine;

    auto advice = adviseToolpathRuntime(
        cfg,
        75.0f * 60.0f * 60.0f,
        9742123);

    ASSERT_TRUE(advice.has_value());
    EXPECT_TRUE(advice->warn);
    EXPECT_EQ(advice->suggestedPreset, StepoverPreset::Fine);
    EXPECT_FLOAT_EQ(advice->suggestedPercent, 8.0f);
    EXPECT_LT(advice->estimatedSeconds, 12.0f * 60.0f * 60.0f);
    EXPECT_LT(advice->estimatedLines, 1000000);
}

TEST(ToolpathAdvisor, ReportsWhenCoarsestPresetCannotReachTarget)
{
    ToolpathConfig cfg;
    cfg.stepoverPreset = StepoverPreset::Rough;

    auto advice = adviseToolpathRuntime(
        cfg,
        100.0f * 60.0f * 60.0f,
        2000000);

    ASSERT_TRUE(advice.has_value());
    EXPECT_EQ(advice->suggestedPreset, StepoverPreset::Roughing);
    EXPECT_FALSE(advice->reachesTarget);
}

#include <gtest/gtest.h>

#include "core/config/input_binding.h"
#include "ui/widgets/status_tips.h"

TEST(StatusTips, WorkshopViewportUsesConfiguredLightBindings) {
    dw::StatusTipState state;
    state.lightDirection = {dw::Mod_Alt, dw::InputType::MouseButton, 1};
    state.lightIntensity = {dw::Mod_Alt | dw::Mod_Shift, dw::InputType::MouseButton, 2};
    state.hasLoadedModel = true;

    auto tips = dw::buildStatusTips(dw::StatusTipContext::WorkshopViewport, state);

    ASSERT_EQ(tips.size(), 3u);
    EXPECT_NE(tips[0].find("Alt+RMB"), std::string::npos);
    EXPECT_NE(tips[0].find("move the light"), std::string::npos);
    EXPECT_NE(tips[1].find("Alt+Shift+MMB"), std::string::npos);
    EXPECT_NE(tips[2].find("Recalculate Normals"), std::string::npos);
}

TEST(StatusTips, WorkshopViewportSkipsModelRepairTipWithoutLoadedModel) {
    dw::StatusTipState state;
    state.lightDirection = {dw::Mod_Alt, dw::InputType::MouseButton, 1};
    state.lightIntensity = {dw::Mod_Alt | dw::Mod_Shift, dw::InputType::MouseButton, 2};
    state.hasLoadedModel = false;

    auto tips = dw::buildStatusTips(dw::StatusTipContext::WorkshopViewport, state);

    ASSERT_EQ(tips.size(), 3u);
    EXPECT_EQ(tips[2].find("Recalculate Normals"), std::string::npos);
}

TEST(StatusTips, SenderReflectsConnectionAndStreamingState) {
    dw::StatusTipState disconnected;
    disconnected.feedOverridePlus = {dw::Mod_Ctrl, dw::InputType::Key, 0};
    disconnected.feedOverrideMinus = {dw::Mod_Ctrl, dw::InputType::Key, 1};

    auto disconnectedTips = dw::buildStatusTips(dw::StatusTipContext::Sender, disconnected);
    ASSERT_FALSE(disconnectedTips.empty());
    EXPECT_NE(disconnectedTips[0].find("connect"), std::string::npos);

    dw::StatusTipState streaming;
    streaming.cncConnected = true;
    streaming.cncStreaming = true;

    auto streamingTips = dw::buildStatusTips(dw::StatusTipContext::Sender, streaming);
    ASSERT_FALSE(streamingTips.empty());
    EXPECT_NE(streamingTips[0].find("active stream"), std::string::npos);
}

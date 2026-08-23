#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "core/carve/direct_carve_probe_tool_diameter.h"

namespace {

dw::VtdbToolGeometry tool(std::string id, dw::f64 diameter) {
    dw::VtdbToolGeometry value;
    value.id = std::move(id);
    value.tool_type = dw::VtdbToolType::EndMill;
    value.units = dw::VtdbUnits::Metric;
    value.diameter = diameter;
    value.num_flutes = 2;
    return value;
}

} // namespace

TEST(DirectCarveProbeToolDiameter, AutomaticValueFollowsChangedInitialTool) {
    dw::carve::DirectCarveProbeToolDiameter state;
    const auto clearingA = tool("clear-a", 6.0);
    const auto clearingB = tool("clear-b", 12.0);

    EXPECT_TRUE(state.refreshAutomatic(&clearingA));
    EXPECT_FLOAT_EQ(state.valueMm(), 6.0F);
    EXPECT_TRUE(state.refreshAutomatic(&clearingB));
    EXPECT_FLOAT_EQ(state.valueMm(), 12.0F);
}

TEST(DirectCarveProbeToolDiameter, AutomaticValueFallsBackFromClearingToFinishing) {
    dw::carve::DirectCarveProbeToolDiameter state;
    const auto clearing = tool("clear", 12.0);
    const auto finishing = tool("finish", 3.175);

    ASSERT_TRUE(state.refreshAutomatic(&clearing));
    EXPECT_TRUE(state.refreshAutomatic(&finishing));
    EXPECT_FLOAT_EQ(state.valueMm(), 3.175F);
}

TEST(DirectCarveProbeToolDiameter, AutomaticValueTracksGeometryCorrectionForSameUuid) {
    dw::carve::DirectCarveProbeToolDiameter state;
    auto corrected = tool("same-tool", 6.0);
    ASSERT_TRUE(state.refreshAutomatic(&corrected));

    // Supplier/library edits retain the UUID, but compensation must follow
    // the corrected physical diameter rather than the stale cached value.
    corrected.diameter = 8.0;

    EXPECT_TRUE(state.refreshAutomatic(&corrected));
    EXPECT_FLOAT_EQ(state.valueMm(), 8.0F);
}

TEST(DirectCarveProbeToolDiameter, ManualOverrideSurvivesToolChangesUntilReleased) {
    dw::carve::DirectCarveProbeToolDiameter state;
    const auto clearing = tool("clear", 12.0);
    const auto finishing = tool("finish", 3.175);
    ASSERT_TRUE(state.refreshAutomatic(&clearing));

    state.setManualValue(4.0F);
    EXPECT_FALSE(state.refreshAutomatic(&finishing));
    EXPECT_TRUE(state.manualOverride());
    EXPECT_FLOAT_EQ(state.valueMm(), 4.0F);

    EXPECT_TRUE(state.resumeAutomatic(&finishing));
    EXPECT_FALSE(state.manualOverride());
    EXPECT_FLOAT_EQ(state.valueMm(), 3.175F);
}

TEST(DirectCarveProbeToolDiameter, EquivalentImperialDiameterUsesMillimeters) {
    dw::carve::DirectCarveProbeToolDiameter state;
    auto imperial = tool("imperial", 0.25);
    imperial.units = dw::VtdbUnits::Imperial;

    EXPECT_TRUE(state.refreshAutomatic(&imperial));
    EXPECT_NEAR(state.valueMm(), 6.35F, 0.0001F);
}

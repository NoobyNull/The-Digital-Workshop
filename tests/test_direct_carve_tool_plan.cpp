#include <gtest/gtest.h>

#include "core/carve/direct_carve_tool_plan.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace {

dw::VtdbToolGeometry makeTool(std::string id, dw::VtdbToolType type, dw::f64 diameter = 6.35) {
    dw::VtdbToolGeometry tool;
    tool.id = std::move(id);
    tool.tool_type = type;
    tool.units = dw::VtdbUnits::Metric;
    tool.diameter = diameter;
    tool.num_flutes = 2;
    tool.flute_length = 20.0;
    return tool;
}

} // namespace

TEST(DirectCarveToolPlan, LegacyDefaultIsAutomaticWithNoToolIntent) {
    dw::carve::DirectCarveToolPlan plan;

    EXPECT_EQ(plan.clearingMode(), dw::carve::ClearingToolMode::Automatic);
    EXPECT_FALSE(plan.finishingIntent().has_value());
    EXPECT_FALSE(plan.clearingIntent().has_value());
    EXPECT_FALSE(plan.effectiveClearingTool().has_value());
}

TEST(DirectCarveToolPlan, AutomaticModeResolvesSuppliedRecommendationOnly) {
    dw::carve::DirectCarveToolPlan plan;
    const auto recommended = makeTool("roughing", dw::VtdbToolType::EndMill, 12.0);

    const auto resolved = plan.resolveClearingIntent(recommended);

    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->id, "roughing");
    EXPECT_FALSE(plan.effectiveClearingTool().has_value());
    EXPECT_FALSE(plan.resolveClearingIntent(std::nullopt).has_value());
}

TEST(DirectCarveToolPlan, SelectedModeResolvesExplicitToolNotRecommendation) {
    dw::carve::DirectCarveToolPlan plan;
    const auto selected = makeTool("selected", dw::VtdbToolType::Radiused, 9.0);
    const auto recommended = makeTool("recommended", dw::VtdbToolType::EndMill, 12.0);

    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Clearing, selected));
    const auto resolved = plan.resolveClearingIntent(recommended);

    EXPECT_EQ(plan.clearingMode(), dw::carve::ClearingToolMode::Selected);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->id, "selected");
}

TEST(DirectCarveToolPlan, DisabledModeResolvesNothingButPreservesIntent) {
    dw::carve::DirectCarveToolPlan plan;
    const auto selected = makeTool("selected", dw::VtdbToolType::EndMill, 9.0);
    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Clearing, selected));

    plan.setClearingMode(dw::carve::ClearingToolMode::Disabled);

    EXPECT_FALSE(plan.resolveClearingIntent(selected).has_value());
    ASSERT_TRUE(plan.clearingIntent().has_value());
    EXPECT_EQ(plan.clearingIntent()->id, "selected");
}

TEST(DirectCarveToolPlan, SelectionCompletenessTracksRequiredAndOptionalRoles) {
    dw::carve::DirectCarveToolPlan plan;
    EXPECT_FALSE(plan.selectionComplete());

    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Finishing,
                                makeTool("finish", dw::VtdbToolType::BallNose, 3.0)));
    EXPECT_TRUE(plan.selectionComplete());

    plan.setClearingMode(dw::carve::ClearingToolMode::Selected);
    EXPECT_FALSE(plan.selectionComplete());
    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Clearing,
                                makeTool("clear", dw::VtdbToolType::EndMill, 6.0)));
    EXPECT_TRUE(plan.selectionComplete());

    plan.clearTool(dw::carve::DirectCarveToolPickerRole::Clearing);
    plan.setClearingMode(dw::carve::ClearingToolMode::Disabled);
    EXPECT_TRUE(plan.selectionComplete());
}

TEST(DirectCarveToolPlan, ReplacingFinishingPreservesClearingSelection) {
    dw::carve::DirectCarveToolPlan plan;
    const auto clearing = makeTool("clearing", dw::VtdbToolType::EndMill, 12.0);
    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Clearing, clearing));

    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Finishing,
                                makeTool("finish-a", dw::VtdbToolType::BallNose)));
    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Finishing,
                                makeTool("finish-b", dw::VtdbToolType::VBit)));

    ASSERT_TRUE(plan.finishingIntent().has_value());
    EXPECT_EQ(plan.finishingIntent()->id, "finish-b");
    ASSERT_TRUE(plan.clearingIntent().has_value());
    EXPECT_EQ(plan.clearingIntent()->id, "clearing");
    EXPECT_EQ(plan.clearingMode(), dw::carve::ClearingToolMode::Selected);
}

TEST(DirectCarveToolPlan, ReplacingClearingPreservesFinishingSelection) {
    dw::carve::DirectCarveToolPlan plan;
    const auto finishing = makeTool("finishing", dw::VtdbToolType::TaperedBallNose, 3.0);
    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Finishing, finishing));

    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Clearing,
                                makeTool("clear-a", dw::VtdbToolType::EndMill)));
    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Clearing,
                                makeTool("clear-b", dw::VtdbToolType::Radiused)));

    ASSERT_TRUE(plan.finishingIntent().has_value());
    EXPECT_EQ(plan.finishingIntent()->id, "finishing");
    ASSERT_TRUE(plan.clearingIntent().has_value());
    EXPECT_EQ(plan.clearingIntent()->id, "clear-b");
}

TEST(DirectCarveToolPlan, SameFlatToolMayServeBothRoles) {
    dw::carve::DirectCarveToolPlan plan;
    const auto shared = makeTool("shared", dw::VtdbToolType::EndMill);

    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Finishing, shared));
    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Clearing, shared));

    ASSERT_TRUE(plan.finishingIntent().has_value());
    ASSERT_TRUE(plan.clearingIntent().has_value());
    EXPECT_TRUE(
        dw::carve::sameDirectCarveToolIdentity(*plan.finishingIntent(), *plan.clearingIntent()));
}

TEST(DirectCarveToolPlan, ClearingSelectionRejectsNonFlatToolsWithoutMutation) {
    dw::carve::DirectCarveToolPlan plan;
    const auto valid = makeTool("valid", dw::VtdbToolType::EndMill);
    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Clearing, valid));

    const auto rejected = plan.selectTool(dw::carve::DirectCarveToolPickerRole::Clearing,
                                          makeTool("ball", dw::VtdbToolType::BallNose));

    EXPECT_FALSE(rejected);
    EXPECT_EQ(rejected.error, dw::carve::DirectCarveToolSelectionError::UnsupportedClearingTool);
    ASSERT_TRUE(plan.clearingIntent().has_value());
    EXPECT_EQ(plan.clearingIntent()->id, "valid");
    EXPECT_EQ(plan.clearingMode(), dw::carve::ClearingToolMode::Selected);
}

TEST(DirectCarveToolPlan, IncompleteGeometryIsRejectedWithoutMutation) {
    dw::carve::DirectCarveToolPlan plan;
    auto incomplete = makeTool("incomplete", dw::VtdbToolType::EndMill, 0.0);

    const auto rejected = plan.selectTool(dw::carve::DirectCarveToolPickerRole::Finishing,
                                          incomplete);

    EXPECT_FALSE(rejected);
    EXPECT_EQ(rejected.error, dw::carve::DirectCarveToolSelectionError::IncompleteToolGeometry);
    EXPECT_FALSE(plan.finishingIntent().has_value());
}

TEST(DirectCarveToolPlan, ManualGeometryStoresOnlyTypeRelevantDetail) {
    const auto endMill =
        dw::carve::makeDirectCarveManualTool(dw::VtdbToolType::EndMill, 6.35, 2, 90.0, 3.175);
    EXPECT_DOUBLE_EQ(endMill.diameter, 6.35);
    EXPECT_DOUBLE_EQ(endMill.included_angle, 0.0);
    EXPECT_DOUBLE_EQ(endMill.tip_radius, 0.0);

    const auto vBit =
        dw::carve::makeDirectCarveManualTool(dw::VtdbToolType::VBit, 6.35, 2, 60.0, 3.175);
    EXPECT_DOUBLE_EQ(vBit.included_angle, 60.0);
    EXPECT_DOUBLE_EQ(vBit.tip_radius, 0.0);

    const auto radiused =
        dw::carve::makeDirectCarveManualTool(dw::VtdbToolType::Radiused, 6.35, 2, 90.0, 0.5);
    EXPECT_DOUBLE_EQ(radiused.included_angle, 0.0);
    EXPECT_DOUBLE_EQ(radiused.tip_radius, 0.5);
}

TEST(DirectCarveToolPlan, EndMillAndRadiusedAreTheOnlyClearingTypes) {
    EXPECT_TRUE(dw::carve::isSupportedClearingTool(makeTool("end", dw::VtdbToolType::EndMill)));
    EXPECT_TRUE(
        dw::carve::isSupportedClearingTool(makeTool("radiused", dw::VtdbToolType::Radiused)));
    EXPECT_FALSE(dw::carve::isSupportedClearingTool(makeTool("v", dw::VtdbToolType::VBit)));
    EXPECT_FALSE(dw::carve::isSupportedClearingTool(makeTool("ball", dw::VtdbToolType::BallNose)));
}

TEST(DirectCarveToolPlan, EffectiveClearingRequiresConfirmedNonemptyPass) {
    dw::carve::DirectCarveToolPlan plan;
    const auto clearing = makeTool("clearing", dw::VtdbToolType::EndMill, 12.0);
    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Clearing, clearing));

    EXPECT_FALSE(plan.confirmEffectiveClearing(clearing, false));
    EXPECT_FALSE(plan.effectiveClearingTool().has_value());
    EXPECT_FALSE(plan.confirmEffectiveClearing(std::nullopt, true));
    EXPECT_FALSE(plan.effectiveClearingTool().has_value());
    EXPECT_TRUE(plan.confirmEffectiveClearing(clearing, true));
    ASSERT_TRUE(plan.effectiveClearingTool().has_value());
    EXPECT_EQ(plan.effectiveClearingTool()->id, "clearing");
}

TEST(DirectCarveToolPlan, DisabledModeCannotConfirmAnEffectiveClearingTool) {
    dw::carve::DirectCarveToolPlan plan;
    const auto clearing = makeTool("clearing", dw::VtdbToolType::EndMill, 12.0);
    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Clearing, clearing));
    plan.setClearingMode(dw::carve::ClearingToolMode::Disabled);

    EXPECT_FALSE(plan.confirmEffectiveClearing(clearing, true));
    EXPECT_FALSE(plan.effectiveClearingTool().has_value());
}

TEST(DirectCarveToolPlan, SelectedModeConfirmsOnlyTheSelectedClearingIdentity) {
    dw::carve::DirectCarveToolPlan plan;
    const auto selected = makeTool("selected", dw::VtdbToolType::EndMill, 6.0);
    const auto different = makeTool("different", dw::VtdbToolType::EndMill, 12.0);
    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Clearing, selected));

    EXPECT_FALSE(plan.confirmEffectiveClearing(different, true));
    EXPECT_FALSE(plan.effectiveClearingTool().has_value());
    EXPECT_TRUE(plan.confirmEffectiveClearing(selected, true));
    ASSERT_TRUE(plan.effectiveClearingTool().has_value());
    EXPECT_EQ(plan.effectiveClearingTool()->id, "selected");
}

TEST(DirectCarveToolPlan, EffectiveInvalidationDoesNotEraseIntentOrMode) {
    dw::carve::DirectCarveToolPlan plan;
    const auto finishing = makeTool("finishing", dw::VtdbToolType::BallNose, 3.0);
    const auto clearing = makeTool("clearing", dw::VtdbToolType::EndMill, 12.0);
    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Finishing, finishing));
    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Clearing, clearing));
    ASSERT_TRUE(plan.confirmEffectiveClearing(clearing, true));

    plan.invalidateEffectiveClearing();

    EXPECT_FALSE(plan.effectiveClearingTool().has_value());
    ASSERT_TRUE(plan.finishingIntent().has_value());
    EXPECT_EQ(plan.finishingIntent()->id, "finishing");
    ASSERT_TRUE(plan.clearingIntent().has_value());
    EXPECT_EQ(plan.clearingIntent()->id, "clearing");
    EXPECT_EQ(plan.clearingMode(), dw::carve::ClearingToolMode::Selected);
}

TEST(DirectCarveToolPlan, ToolChangesInvalidateOnlyTheEffectiveResult) {
    dw::carve::DirectCarveToolPlan plan;
    const auto clearing = makeTool("clearing", dw::VtdbToolType::EndMill, 12.0);
    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Clearing, clearing));
    ASSERT_TRUE(plan.confirmEffectiveClearing(clearing, true));

    ASSERT_TRUE(plan.selectTool(dw::carve::DirectCarveToolPickerRole::Finishing,
                                makeTool("finishing", dw::VtdbToolType::VBit)));

    EXPECT_FALSE(plan.effectiveClearingTool().has_value());
    ASSERT_TRUE(plan.clearingIntent().has_value());
    EXPECT_EQ(plan.clearingIntent()->id, "clearing");
}

TEST(DirectCarveToolPlan, StableIdentityPrefersUuidOverGeometry) {
    auto first = makeTool("supplier-uuid", dw::VtdbToolType::EndMill, 6.0);
    auto changedGeometry = first;
    changedGeometry.diameter = 12.0;
    auto differentUuid = first;
    differentUuid.id = "other-uuid";

    EXPECT_EQ(dw::carve::stableDirectCarveToolIdentity(first), "uuid:supplier-uuid");
    EXPECT_TRUE(dw::carve::sameDirectCarveToolIdentity(first, changedGeometry));
    EXPECT_FALSE(dw::carve::sameDirectCarveToolIdentity(first, differentUuid));
}

TEST(DirectCarveToolPlan, ManualToolIdentityFallsBackToPhysicalGeometry) {
    auto first = makeTool("", dw::VtdbToolType::BallNose, 3.175);
    first.name_format = "First label";
    auto relabeled = first;
    relabeled.name_format = "A different label";
    auto differentGeometry = first;
    differentGeometry.tip_radius = 0.25;

    EXPECT_TRUE(dw::carve::sameDirectCarveToolIdentity(first, relabeled));
    EXPECT_FALSE(dw::carve::sameDirectCarveToolIdentity(first, differentGeometry));
}

TEST(DirectCarveToolPlan, StableIdentitySurvivesFilteringAndReordering) {
    const auto alpha = makeTool("alpha", dw::VtdbToolType::EndMill, 3.0);
    const auto beta = makeTool("beta", dw::VtdbToolType::EndMill, 6.0);
    const std::string selected = dw::carve::stableDirectCarveToolIdentity(beta);
    std::vector<dw::VtdbToolGeometry> visible{beta, alpha};
    std::reverse(visible.begin(), visible.end());

    const auto match = std::find_if(visible.begin(), visible.end(), [&](const auto& tool) {
        return dw::carve::stableDirectCarveToolIdentity(tool) == selected;
    });

    ASSERT_NE(match, visible.end());
    EXPECT_EQ(match->id, "beta");
}

TEST(DirectCarveToolPlan, ClearingModeKeysRoundTripWithLegacyFallback) {
    using dw::carve::ClearingToolMode;

    EXPECT_EQ(dw::carve::clearingToolModeKey(ClearingToolMode::Automatic), "automatic");
    EXPECT_EQ(dw::carve::clearingToolModeKey(ClearingToolMode::Selected), "selected");
    EXPECT_EQ(dw::carve::clearingToolModeKey(ClearingToolMode::Disabled), "disabled");
    EXPECT_EQ(dw::carve::parseClearingToolModeKey("SELECTED"), ClearingToolMode::Selected);
    EXPECT_EQ(dw::carve::parseClearingToolModeKey("disabled"), ClearingToolMode::Disabled);
    EXPECT_EQ(dw::carve::parseClearingToolModeKey(""), ClearingToolMode::Automatic);
    EXPECT_EQ(dw::carve::parseClearingToolModeKey("legacy-or-unknown"),
              ClearingToolMode::Automatic);
}

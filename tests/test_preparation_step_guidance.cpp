#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "modules/carve_preparation/preparation_step_guidance.h"

namespace {

using namespace dw;
using namespace dw::carve_preparation;

constexpr workshop::ProjectId kProject{41};
constexpr workshop::ProjectItemRef kModel{kProject, workshop::ProjectItemId{401}};
constexpr workshop::ProjectItemRef kOperation{kProject, workshop::ProjectItemId{402}};
constexpr workshop::LibraryItemRef kSource{workshop::LibraryItemKind::Model,
                                           workshop::LibraryItemId{801}};

PrepareCarvePin makePin() {
    return {kProject,
            kModel,
            kSource,
            kOperation,
            PreparationToken{91},
            PreparationRevision{13}};
}

PreparationStageProjection stage(
    PreparationStageId id,
    PreparationStageState state,
    std::vector<PreparationBlocker> blockers = {}) {
    return {id, state, std::move(blockers)};
}

} // namespace

TEST(PreparationStepGuidance, ExplainsWhyMaterialAndBlankComeBeforeToolChoice) {
    PreparationStepFacts material(
        makePin(),
        stage(PreparationStageId::MaterialAndBlank, PreparationStageState::Complete));
    const auto materialView = buildPreparationStepPresentation(material);

    EXPECT_EQ(materialView.title, "Material & Blank");
    EXPECT_NE(materialView.whyItMatters.find("come before tool choice"),
              std::string::npos);
    EXPECT_NE(materialView.nextGuidance.find("recommendations can now use the material"),
              std::string::npos);
    EXPECT_EQ(materialView.primaryAction.kind, PreparationPrimaryActionKind::Continue);
    EXPECT_TRUE(materialView.primaryAction.available);

    PreparationStepFacts lockedTool(
        makePin(),
        stage(PreparationStageId::ChooseTool,
              PreparationStageState::Locked,
              {{PreparationStageId::ChooseTool,
                PreparationBlockerCode::PreviousStageIncomplete,
                PreparationEvidence::Unsatisfied}}));
    const auto toolView = buildPreparationStepPresentation(lockedTool);
    EXPECT_NE(toolView.recommendationRationale.find("Finish Material & Blank first"),
              std::string::npos);
    EXPECT_FALSE(toolView.primaryAction.available);
}

TEST(PreparationStepGuidance, PreservesTheRecommendationReasonAsVisibleCopy) {
    const std::string reason =
        "Its small ball nose reaches the design detail and has cutting data for walnut.";
    PreparationStepFacts facts(
        makePin(),
        stage(PreparationStageId::ChooseTool, PreparationStageState::NeedsAttention),
        {},
        PreparationRecommendationFact{"1/8 in ball nose", reason});

    const auto view = buildPreparationStepPresentation(facts);

    EXPECT_EQ(view.recommendationLabel, "1/8 in ball nose");
    EXPECT_EQ(view.recommendationRationale, reason);
}

TEST(PreparationStepGuidance, TurnsMissingFactsIntoSpecificNextGuidance) {
    PreparationStepFacts facts(
        makePin(),
        stage(PreparationStageId::DesignAndSize,
              PreparationStageState::Available,
              {{PreparationStageId::DesignAndSize,
                PreparationBlockerCode::DesignLoadUnknown,
                PreparationEvidence::Unknown}}));

    const auto view = buildPreparationStepPresentation(facts);

    EXPECT_EQ(view.title, "Design & Size");
    EXPECT_EQ(view.nextGuidance,
              "Design details are still loading. Wait for them before continuing.");
    EXPECT_EQ(view.primaryAction.kind, PreparationPrimaryActionKind::Continue);
    EXPECT_FALSE(view.primaryAction.available);
}

TEST(PreparationStepGuidance, RetainsAdvancedDisclosureIndependentlyForEveryStep) {
    const PreparationAdvancedState initial;
    const auto designOpen =
        initial.withExpansion(PreparationStageId::DesignAndSize, true);
    const auto toolOpen = designOpen.withExpansion(PreparationStageId::ChooseTool, true);
    const auto toolClosed = toolOpen.withExpansion(PreparationStageId::ChooseTool, false);

    EXPECT_TRUE(toolClosed.expanded(PreparationStageId::DesignAndSize));
    EXPECT_FALSE(toolClosed.expanded(PreparationStageId::MaterialAndBlank));
    EXPECT_FALSE(toolClosed.expanded(PreparationStageId::ChooseTool));

    PreparationStepFacts facts(
        makePin(),
        stage(PreparationStageId::DesignAndSize, PreparationStageState::Complete),
        toolClosed);
    const auto view = buildPreparationStepPresentation(facts);
    EXPECT_TRUE(view.advanced.available);
    EXPECT_TRUE(view.advanced.expanded);
    EXPECT_EQ(view.advanced.label, "Hide Advanced");

    const auto disabled = buildPreparationStepPresentation(
        facts, PreparationStepGuidanceOptions{false, false});
    EXPECT_FALSE(disabled.advanced.available);
    EXPECT_TRUE(disabled.advanced.expanded);
    EXPECT_EQ(disabled.primaryAction.kind, PreparationPrimaryActionKind::None);
    EXPECT_FALSE(disabled.primaryAction.available);
}

TEST(PreparationStepGuidance, PreviewPrimaryActionChangesAfterGeneration) {
    PreparationStepFacts missing(
        makePin(),
        stage(PreparationStageId::CarvePreview,
              PreparationStageState::NeedsAttention,
              {{PreparationStageId::CarvePreview,
                PreparationBlockerCode::PreviewNotGenerated,
                PreparationEvidence::Unsatisfied}}));
    const auto generate = buildPreparationStepPresentation(missing);
    EXPECT_EQ(generate.primaryAction.kind,
              PreparationPrimaryActionKind::GeneratePreview);
    EXPECT_EQ(generate.primaryAction.label, "Generate Carve Preview");
    EXPECT_TRUE(generate.primaryAction.available);

    PreparationStepFacts complete(
        makePin(),
        stage(PreparationStageId::CarvePreview, PreparationStageState::Complete));
    const auto continueView = buildPreparationStepPresentation(complete);
    EXPECT_EQ(continueView.primaryAction.kind, PreparationPrimaryActionKind::Continue);
    EXPECT_EQ(continueView.primaryAction.label, "Continue to Machine Setup");
    EXPECT_TRUE(continueView.primaryAction.available);
}

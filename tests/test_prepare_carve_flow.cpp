#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <variant>
#include <vector>

#include "modules/carve_preparation/prepare_carve_flow.h"

namespace {

using namespace dw;
using namespace dw::carve_preparation;

constexpr workshop::ProjectId kProject{31};
constexpr workshop::ProjectItemRef kModel{kProject, workshop::ProjectItemId{301}};
constexpr workshop::ProjectItemRef kOperation{kProject, workshop::ProjectItemId{302}};
constexpr workshop::LibraryItemRef kSource{workshop::LibraryItemKind::Model,
                                           workshop::LibraryItemId{901}};
constexpr PreparationToken kToken{71};
constexpr PreparationRevision kProjectRevision{11};

PrepareCarvePin makePin(PreparationToken token = kToken) {
    return {kProject, kModel, kSource, kOperation, token, kProjectRevision};
}

PreparationReadinessSnapshot readyFacts(
    PrepareCarvePin pin = makePin(),
    PreparationEditRevision editRevision = PreparationEditRevision{5}) {
    PreparationReadinessSnapshot facts(std::move(pin), editRevision);
    facts.modelLoaded = PreparationEvidence::Satisfied;
    facts.modelFitsMachine = PreparationEvidence::Satisfied;
    facts.materialSelected = PreparationEvidence::Satisfied;
    facts.blankSpecified = PreparationEvidence::Satisfied;
    facts.modelFitsBlank = PreparationEvidence::Satisfied;
    facts.finishingToolSelected = PreparationEvidence::Satisfied;
    facts.toolSetupConfirmed = PreparationEvidence::Satisfied;
    facts.toolpathGenerated = PreparationEvidence::Satisfied;
    facts.toolpathFresh = PreparationEvidence::Satisfied;
    return facts;
}

bool hasBlocker(const PreparationStageProjection& stage,
                PreparationBlockerCode code,
                PreparationEvidence evidence) {
    return std::any_of(stage.blockers.begin(), stage.blockers.end(),
                       [&](const PreparationBlocker& blocker) {
                           return blocker.code == code && blocker.evidence == evidence;
                       });
}

} // namespace

TEST(PrepareCarveFlow, PublishesExactlyTheBeginnerPreparationOrder) {
    const auto& order = preparationStageOrder();

    static_assert(std::tuple_size_v<std::remove_reference_t<decltype(order)>> == 4);
    EXPECT_EQ(order[0], PreparationStageId::DesignAndSize);
    EXPECT_EQ(order[1], PreparationStageId::MaterialAndBlank);
    EXPECT_EQ(order[2], PreparationStageId::ChooseTool);
    EXPECT_EQ(order[3], PreparationStageId::CarvePreview);
}

TEST(PrepareCarveFlow, UnknownEvidenceRemainsExplicitAndDoesNotUnlockLaterStages) {
    PrepareCarveFlow flow;
    PreparationReadinessSnapshot unknown(makePin(), PreparationEditRevision{1});

    const auto result = flow.dispatch(BeginPreparation{unknown});

    ASSERT_EQ(result.status, PrepareCarveTransitionStatus::Applied);
    ASSERT_TRUE(result.snapshot.active);
    ASSERT_NE(result.snapshot.pin(), nullptr);
    EXPECT_TRUE(*result.snapshot.pin() == makePin());
    const auto& design = result.snapshot.stage(PreparationStageId::DesignAndSize);
    EXPECT_EQ(design.state, PreparationStageState::Available);
    EXPECT_TRUE(hasBlocker(design,
                           PreparationBlockerCode::DesignLoadUnknown,
                           PreparationEvidence::Unknown));
    EXPECT_TRUE(hasBlocker(design,
                           PreparationBlockerCode::MachineFitUnknown,
                           PreparationEvidence::Unknown));
    const auto& material = result.snapshot.stage(PreparationStageId::MaterialAndBlank);
    EXPECT_EQ(material.state, PreparationStageState::Locked);
    EXPECT_TRUE(hasBlocker(material,
                           PreparationBlockerCode::PreviousStageIncomplete,
                           PreparationEvidence::Unsatisfied));
}

TEST(PrepareCarveFlow, UnsatisfiedEvidenceIsDistinctFromUnknownEvidence) {
    PrepareCarveFlow flow;
    PreparationReadinessSnapshot facts(makePin());
    facts.modelLoaded = PreparationEvidence::Satisfied;
    facts.modelFitsMachine = PreparationEvidence::Unsatisfied;

    const auto result = flow.dispatch(BeginPreparation{facts});

    const auto& design = result.snapshot.stage(PreparationStageId::DesignAndSize);
    EXPECT_EQ(design.state, PreparationStageState::NeedsAttention);
    EXPECT_TRUE(hasBlocker(design,
                           PreparationBlockerCode::DesignDoesNotFitMachine,
                           PreparationEvidence::Unsatisfied));
    EXPECT_FALSE(hasBlocker(design,
                            PreparationBlockerCode::MachineFitUnknown,
                            PreparationEvidence::Unknown));
}

TEST(PrepareCarveFlow, MaterialCompletionIsAHardGateForToolSelection) {
    PrepareCarveFlow flow;
    auto facts = readyFacts();
    facts.materialSelected = PreparationEvidence::Unsatisfied;

    auto result = flow.dispatch(BeginPreparation{facts});
    ASSERT_EQ(result.snapshot.stage(PreparationStageId::DesignAndSize).state,
              PreparationStageState::Complete);
    EXPECT_EQ(result.snapshot.stage(PreparationStageId::MaterialAndBlank).state,
              PreparationStageState::NeedsAttention);
    EXPECT_EQ(result.snapshot.stage(PreparationStageId::ChooseTool).state,
              PreparationStageState::Locked);

    result = flow.dispatch(OpenPreparationStage{PreparationStageId::ChooseTool});
    EXPECT_EQ(result.status, PrepareCarveTransitionStatus::Rejected);
    EXPECT_EQ(result.reason, PrepareCarveTransitionReason::StageLocked);
    ASSERT_TRUE(result.blocker.has_value());
    EXPECT_EQ(result.blocker->code, PreparationBlockerCode::PreviousStageIncomplete);
    EXPECT_FALSE(result.effect.has_value());
}

TEST(PrepareCarveFlow, ContinueTraversesOnlyTheCanonicalStageSequence) {
    PrepareCarveFlow flow;
    ASSERT_EQ(flow.dispatch(BeginPreparation{readyFacts()}).status,
              PrepareCarveTransitionStatus::Applied);

    for (std::size_t next = 1; next < preparationStageOrder().size(); ++next) {
        const auto result = flow.dispatch(ContinuePreparation{});
        ASSERT_EQ(result.status, PrepareCarveTransitionStatus::Applied);
        EXPECT_EQ(result.snapshot.activeStage, preparationStageOrder()[next]);
        EXPECT_FALSE(result.effect.has_value());
    }

    const auto complete = flow.dispatch(ContinuePreparation{});
    EXPECT_EQ(complete.status, PrepareCarveTransitionStatus::EffectIssued);
    EXPECT_EQ(complete.snapshot.activeStage, PreparationStageId::CarvePreview);
    ASSERT_TRUE(complete.effect.has_value());
    const auto* ready = std::get_if<PreparationReady>(&*complete.effect);
    ASSERT_NE(ready, nullptr);
    EXPECT_TRUE(ready->readiness.pin() == makePin());
}

TEST(PrepareCarveFlow, ContinueReturnsTheExactCurrentStageBlocker) {
    PrepareCarveFlow flow;
    PreparationReadinessSnapshot facts(makePin());
    facts.modelLoaded = PreparationEvidence::Unsatisfied;
    facts.modelFitsMachine = PreparationEvidence::Satisfied;
    ASSERT_EQ(flow.dispatch(BeginPreparation{facts}).status,
              PrepareCarveTransitionStatus::Applied);

    const auto result = flow.dispatch(ContinuePreparation{});

    EXPECT_EQ(result.status, PrepareCarveTransitionStatus::Rejected);
    EXPECT_EQ(result.reason, PrepareCarveTransitionReason::StageIncomplete);
    ASSERT_TRUE(result.blocker.has_value());
    EXPECT_EQ(result.blocker->stage, PreparationStageId::DesignAndSize);
    EXPECT_EQ(result.blocker->code, PreparationBlockerCode::DesignNotLoaded);
    EXPECT_EQ(result.blocker->evidence, PreparationEvidence::Unsatisfied);
}

TEST(PrepareCarveFlow, RefreshRetainsPinAndFallsBackToAnEarlierRegressedStage) {
    PrepareCarveFlow flow;
    ASSERT_EQ(flow.dispatch(BeginPreparation{readyFacts()}).status,
              PrepareCarveTransitionStatus::Applied);
    ASSERT_EQ(flow.dispatch(OpenPreparationStage{PreparationStageId::CarvePreview}).status,
              PrepareCarveTransitionStatus::Applied);

    auto revised = readyFacts(makePin(), PreparationEditRevision{6});
    revised.modelFitsBlank = PreparationEvidence::Unsatisfied;
    const auto result = flow.dispatch(RefreshPreparation{revised});

    EXPECT_EQ(result.status, PrepareCarveTransitionStatus::Applied);
    EXPECT_EQ(result.snapshot.activeStage, PreparationStageId::MaterialAndBlank);
    ASSERT_NE(result.snapshot.pin(), nullptr);
    EXPECT_TRUE(*result.snapshot.pin() == makePin());
    ASSERT_TRUE(result.snapshot.readiness.has_value());
    EXPECT_EQ(result.snapshot.readiness->editRevision(), PreparationEditRevision{6});
    EXPECT_EQ(result.snapshot.stage(PreparationStageId::ChooseTool).state,
              PreparationStageState::Locked);
}

TEST(PrepareCarveFlow, RefreshFromAnotherPinCannotReplaceTheActiveIdentity) {
    PrepareCarveFlow flow;
    const auto original = readyFacts(makePin(), PreparationEditRevision{5});
    ASSERT_EQ(flow.dispatch(BeginPreparation{original}).status,
              PrepareCarveTransitionStatus::Applied);

    const auto result = flow.dispatch(
        RefreshPreparation{readyFacts(makePin(PreparationToken{72}),
                                      PreparationEditRevision{99})});

    EXPECT_EQ(result.status, PrepareCarveTransitionStatus::Rejected);
    EXPECT_EQ(result.reason, PrepareCarveTransitionReason::PinMismatch);
    ASSERT_NE(result.snapshot.pin(), nullptr);
    EXPECT_TRUE(*result.snapshot.pin() == makePin());
    ASSERT_TRUE(result.snapshot.readiness.has_value());
    EXPECT_EQ(result.snapshot.readiness->editRevision(), PreparationEditRevision{5});
    EXPECT_FALSE(result.effect.has_value());
}

TEST(PrepareCarveFlow, RefreshRequiresMonotonicSemanticRevisions) {
    PrepareCarveFlow flow;
    const auto original = readyFacts(makePin(), PreparationEditRevision{5});
    ASSERT_EQ(flow.dispatch(BeginPreparation{original}).status,
              PrepareCarveTransitionStatus::Applied);

    auto conflicting = readyFacts(makePin(), PreparationEditRevision{5});
    conflicting.materialSelected = PreparationEvidence::Unsatisfied;
    const auto conflict = flow.dispatch(RefreshPreparation{conflicting});
    EXPECT_EQ(conflict.status, PrepareCarveTransitionStatus::Rejected);
    EXPECT_EQ(conflict.reason, PrepareCarveTransitionReason::RevisionConflict);

    const auto same = flow.dispatch(RefreshPreparation{original});
    EXPECT_EQ(same.status, PrepareCarveTransitionStatus::Unchanged);
    EXPECT_EQ(same.reason, PrepareCarveTransitionReason::None);

    const auto stale = flow.dispatch(
        RefreshPreparation{readyFacts(makePin(), PreparationEditRevision{4})});
    EXPECT_EQ(stale.status, PrepareCarveTransitionStatus::Rejected);
    EXPECT_EQ(stale.reason, PrepareCarveTransitionReason::StaleRevision);
    ASSERT_TRUE(stale.snapshot.readiness.has_value());
    EXPECT_EQ(stale.snapshot.readiness->editRevision(), PreparationEditRevision{5});
}

TEST(PrepareCarveFlow, PreviewRequestCarriesExactPinAndSemanticEditRevision) {
    PrepareCarveFlow flow;
    auto facts = readyFacts(makePin(), PreparationEditRevision{44});
    facts.toolpathGenerated = PreparationEvidence::Unsatisfied;
    facts.toolpathFresh = PreparationEvidence::Unsatisfied;
    ASSERT_EQ(flow.dispatch(BeginPreparation{facts}).status,
              PrepareCarveTransitionStatus::Applied);

    const auto result = flow.dispatch(GeneratePreparationPreview{});

    EXPECT_EQ(result.status, PrepareCarveTransitionStatus::EffectIssued);
    EXPECT_EQ(result.snapshot.activeStage, PreparationStageId::CarvePreview);
    ASSERT_TRUE(result.effect.has_value());
    const auto* request = std::get_if<PreparationPreviewRequest>(&*result.effect);
    ASSERT_NE(request, nullptr);
    EXPECT_TRUE(request->readiness.pin() == makePin());
    EXPECT_EQ(request->readiness.editRevision(), PreparationEditRevision{44});
    EXPECT_EQ(std::get_if<PreparationSaveRequest>(&*result.effect), nullptr);
}

TEST(PrepareCarveFlow, PreviewRequestCannotBypassMaterialOrToolGates) {
    PrepareCarveFlow flow;
    auto facts = readyFacts();
    facts.materialSelected = PreparationEvidence::Unknown;
    ASSERT_EQ(flow.dispatch(BeginPreparation{facts}).status,
              PrepareCarveTransitionStatus::Applied);

    const auto result = flow.dispatch(GeneratePreparationPreview{});

    EXPECT_EQ(result.status, PrepareCarveTransitionStatus::Rejected);
    EXPECT_EQ(result.reason, PrepareCarveTransitionReason::StageLocked);
    EXPECT_FALSE(result.effect.has_value());
}

TEST(PrepareCarveFlow, PreviewCompletionMustMatchThePendingSemanticRevision) {
    PrepareCarveFlow flow;
    auto initial = readyFacts(makePin(), PreparationEditRevision{44});
    initial.toolpathGenerated = PreparationEvidence::Unsatisfied;
    initial.toolpathFresh = PreparationEvidence::Unsatisfied;
    ASSERT_EQ(flow.dispatch(BeginPreparation{initial}).status,
              PrepareCarveTransitionStatus::Applied);
    ASSERT_EQ(flow.dispatch(GeneratePreparationPreview{}).status,
              PrepareCarveTransitionStatus::EffectIssued);

    auto edited = initial;
    edited = readyFacts(makePin(), PreparationEditRevision{45});
    edited.toolpathGenerated = PreparationEvidence::Unsatisfied;
    edited.toolpathFresh = PreparationEvidence::Unsatisfied;
    ASSERT_EQ(flow.dispatch(RefreshPreparation{edited}).status,
              PrepareCarveTransitionStatus::Applied);

    const auto stale = flow.dispatch(
        CompletePreparationPreview{makePin(), PreparationEditRevision{44}, true});
    EXPECT_EQ(stale.status, PrepareCarveTransitionStatus::Rejected);
    EXPECT_EQ(stale.reason, PrepareCarveTransitionReason::StaleRevision);
    ASSERT_TRUE(stale.snapshot.readiness.has_value());
    EXPECT_EQ(stale.snapshot.readiness->toolpathGenerated,
              PreparationEvidence::Unsatisfied);

    ASSERT_EQ(flow.dispatch(GeneratePreparationPreview{}).status,
              PrepareCarveTransitionStatus::EffectIssued);
    const auto current = flow.dispatch(
        CompletePreparationPreview{makePin(), PreparationEditRevision{45}, true});
    EXPECT_EQ(current.status, PrepareCarveTransitionStatus::Applied);
    ASSERT_TRUE(current.snapshot.readiness.has_value());
    EXPECT_EQ(current.snapshot.readiness->toolpathGenerated,
              PreparationEvidence::Satisfied);
    EXPECT_EQ(current.snapshot.readiness->toolpathFresh,
              PreparationEvidence::Satisfied);
    EXPECT_TRUE(current.snapshot.readiness->hasUnsavedChanges);
    EXPECT_FALSE(current.snapshot.pendingPreviewRevision.has_value());
}

TEST(PrepareCarveFlow, SaveEmitsOnlyForDirtyPreparationAndCarriesExactSnapshot) {
    PrepareCarveFlow cleanFlow;
    ASSERT_EQ(cleanFlow.dispatch(BeginPreparation{readyFacts()}).status,
              PrepareCarveTransitionStatus::Applied);
    const auto clean = cleanFlow.dispatch(SavePreparation{});
    EXPECT_EQ(clean.status, PrepareCarveTransitionStatus::Unchanged);
    EXPECT_EQ(clean.reason, PrepareCarveTransitionReason::NoUnsavedChanges);
    EXPECT_FALSE(clean.effect.has_value());

    PrepareCarveFlow dirtyFlow;
    auto dirtyFacts = readyFacts(makePin(), PreparationEditRevision{88});
    dirtyFacts.hasUnsavedChanges = true;
    ASSERT_EQ(dirtyFlow.dispatch(BeginPreparation{dirtyFacts}).status,
              PrepareCarveTransitionStatus::Applied);
    const auto dirty = dirtyFlow.dispatch(SavePreparation{});
    EXPECT_EQ(dirty.status, PrepareCarveTransitionStatus::EffectIssued);
    ASSERT_TRUE(dirty.effect.has_value());
    const auto* request = std::get_if<PreparationSaveRequest>(&*dirty.effect);
    ASSERT_NE(request, nullptr);
    EXPECT_TRUE(request->readiness.pin() == makePin());
    EXPECT_EQ(request->readiness.editRevision(), PreparationEditRevision{88});
    EXPECT_TRUE(request->readiness.hasUnsavedChanges);

    const auto saved = dirtyFlow.dispatch(
        CompletePreparationSave{makePin(), PreparationEditRevision{88}, true});
    EXPECT_EQ(saved.status, PrepareCarveTransitionStatus::Applied);
    ASSERT_TRUE(saved.snapshot.readiness.has_value());
    EXPECT_FALSE(saved.snapshot.readiness->hasUnsavedChanges);
    EXPECT_FALSE(saved.snapshot.pendingSaveRevision.has_value());
}

TEST(PrepareCarveFlow, DisabledModeIsInertForEveryIntent) {
    PrepareCarveFlow flow(PrepareCarveFlowOptions{false});
    const std::vector<PrepareCarveIntent> intents = {
        BeginPreparation{readyFacts()},
        RefreshPreparation{readyFacts()},
        OpenPreparationStage{PreparationStageId::CarvePreview},
        ContinuePreparation{},
        GeneratePreparationPreview{},
        SavePreparation{},
        CompletePreparationPreview{makePin(), PreparationEditRevision{5}, true},
        CompletePreparationSave{makePin(), PreparationEditRevision{5}, true},
        EndPreparation{makePin()},
    };

    for (const auto& intent : intents) {
        const auto result = flow.dispatch(intent);
        EXPECT_EQ(result.status, PrepareCarveTransitionStatus::Disabled);
        EXPECT_EQ(result.reason, PrepareCarveTransitionReason::ModuleDisabled);
        EXPECT_FALSE(result.snapshot.active);
        EXPECT_EQ(result.snapshot.pin(), nullptr);
        EXPECT_FALSE(result.effect.has_value());
    }
}

TEST(PrepareCarveFlow, EndRequiresExactPinAndMakesTheFlowReusable) {
    PrepareCarveFlow flow;
    ASSERT_EQ(flow.dispatch(BeginPreparation{readyFacts()}).status,
              PrepareCarveTransitionStatus::Applied);

    const auto staleEnd = flow.dispatch(EndPreparation{makePin(PreparationToken{72})});
    EXPECT_EQ(staleEnd.status, PrepareCarveTransitionStatus::Rejected);
    EXPECT_EQ(staleEnd.reason, PrepareCarveTransitionReason::PinMismatch);
    EXPECT_TRUE(staleEnd.snapshot.active);

    const auto ended = flow.dispatch(EndPreparation{makePin()});
    EXPECT_EQ(ended.status, PrepareCarveTransitionStatus::Applied);
    EXPECT_FALSE(ended.snapshot.active);
    EXPECT_EQ(ended.snapshot.pin(), nullptr);
    EXPECT_FALSE(ended.effect.has_value());
    EXPECT_EQ(ended.snapshot.stages[0].id, PreparationStageId::DesignAndSize);

    EXPECT_EQ(flow.dispatch(BeginPreparation{readyFacts()}).status,
              PrepareCarveTransitionStatus::Applied);
}

TEST(PrepareCarveFlow, InvalidPinIsRejectedWithoutStartingOrEmittingEffects) {
    constexpr workshop::ProjectId invalidProject{};
    const PrepareCarvePin invalidPin(invalidProject,
                                     kModel,
                                     kSource,
                                     kOperation,
                                     kToken,
                                     kProjectRevision);
    PrepareCarveFlow flow;

    const auto result = flow.dispatch(
        BeginPreparation{PreparationReadinessSnapshot{invalidPin}});

    EXPECT_EQ(result.status, PrepareCarveTransitionStatus::Rejected);
    EXPECT_EQ(result.reason, PrepareCarveTransitionReason::InvalidPin);
    EXPECT_FALSE(result.snapshot.active);
    EXPECT_FALSE(result.effect.has_value());
}

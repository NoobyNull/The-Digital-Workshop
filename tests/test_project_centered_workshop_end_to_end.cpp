#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "app/carve_preparation_adapter.h"
#include "app/library_workflow_adapter.h"
#include "app/library_workflow_coordinator.h"
#include "app/project_session_integration.h"
#include "core/config/config.h"
#include "core/database/database.h"
#include "core/database/gcode_repository.h"
#include "core/database/model_repository.h"
#include "core/database/project_repository.h"
#include "core/database/schema.h"
#include "core/library/library_manager.h"
#include "core/mesh/hash.h"
#include "core/project/named_project_creation.h"
#include "core/project/project.h"
#include "core/project/project_asset_membership.h"
#include "core/utils/file_utils.h"
#include "modules/carve_preparation/prepare_carve_flow.h"
#include "modules/design_library/library_picker_flow.h"
#include "modules/project_session/project_session.h"
#include "modules/run_coordination/run_coordinator.h"
#include "modules/workshop/project_resume.h"
#include "modules/workshop/project_workshop_controller.h"

namespace {

namespace carve = dw::carve_preparation;
namespace library = dw::design_library;
namespace run = dw::run_coordination;
namespace workshop = dw::workshop;

bool sameLibraryItem(workshop::LibraryItemRef lhs,
                     workshop::LibraryItemRef rhs) noexcept {
    return lhs.kind == rhs.kind && lhs.item == rhs.item;
}

class MemoryResumeStore final : public workshop::ProjectResumeStore {
  public:
    workshop::ProjectResumeLoadResult load() const override {
        if (!bookmark) {
            return {workshop::ProjectResumeLoadStatus::Missing, std::nullopt};
        }
        return {workshop::ProjectResumeLoadStatus::Loaded, bookmark};
    }

    bool save(const workshop::ProjectResumeBookmark& value) override {
        bookmark = value;
        return true;
    }

    bool clear() override {
        bookmark.reset();
        return true;
    }

    std::optional<workshop::ProjectResumeBookmark> bookmark;
};

class ProjectCenteredWorkshopEndToEndTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_root = std::filesystem::temp_directory_path() /
                 ("dw_project_centered_e2e_" +
                  std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::remove_all(m_root);
        ASSERT_TRUE(dw::file::createDirectories(m_root));

        m_previousProjectsDirectory = dw::Config::instance().getProjectsDir();
        dw::Config::instance().setProjectsDir(m_root / "Projects");
        ASSERT_TRUE(m_database.open(":memory:"));
        ASSERT_TRUE(dw::Schema::initialize(m_database));

        m_projects = std::make_unique<dw::ProjectManager>(m_database);
        m_library = std::make_unique<dw::LibraryManager>(m_database);
        m_gcodes = std::make_unique<dw::GCodeRepository>(m_database);
        m_libraryWorkflow = std::make_unique<dw::LibraryWorkflowCoordinator>(
            m_database, *m_library, *m_projects, *m_gcodes);
        m_integration = std::make_unique<dw::ProjectSessionIntegration>(
            m_session, *m_projects);
        m_workshop = std::make_unique<workshop::ProjectWorkshopController>(
            m_session, true);
    }

    void TearDown() override {
        m_workshop.reset();
        m_integration.reset();
        m_libraryWorkflow.reset();
        m_gcodes.reset();
        m_library.reset();
        m_projects.reset();
        m_database.close();
        dw::Config::instance().setProjectsDir(m_previousProjectsDirectory);
        std::filesystem::remove_all(m_root);
    }

    dw::i64 addRiverSignFixture(const std::string& filename,
                                const std::string& name,
                                const std::string& hash) {
        const dw::Path path = dw::Path(CMAKE_SOURCE_DIR) / "tests" / "fixtures" /
                              "ux" / "river_sign" / filename;
        EXPECT_TRUE(dw::file::isFile(path));
        dw::ModelRecord model;
        model.name = name;
        model.hash = hash;
        model.filePath = path;
        model.fileFormat = "stl";
        model.fileSize = dw::file::getFileSize(path).value_or(0);
        model.boundsMin = dw::Vec3(0.0F, 0.0F, 0.0F);
        model.boundsMax = dw::Vec3(120.0F, 60.0F, 6.0F);
        dw::ModelRepository models(m_database);
        const auto id = models.insert(model);
        EXPECT_TRUE(id.has_value());
        return id.value_or(0);
    }

    library::LibraryPickerTransition beginAddToProject(
        workshop::ProjectId project,
        const std::vector<workshop::LibraryItemRef>& membership = {}) {
        auto& picker = m_libraryWorkflow->picker();
        const auto opened = picker.dispatch(
            library::BeginLibraryPicker{
                library::LibraryPickerPurpose::AddToProject,
                "River Sign",
                membership},
            m_session.snapshot());
        EXPECT_EQ(opened.status, library::LibraryPickerTransitionStatus::Applied);
        EXPECT_EQ(opened.snapshot.activeProject, project);

        const auto context = m_session.snapshot();
        const auto navigation = m_workshop->dispatch(
            workshop::NavigateWorkshopIntent{
                workshop::ExperienceMode::Guided,
                context.generation,
                workshop::WorkshopRoute::DesignLibrary});
        EXPECT_TRUE(navigation.accepted());
        return opened;
    }

    void returnFromPicker() {
        auto& picker = m_libraryWorkflow->picker();
        const auto cancelled = picker.dispatch(
            library::CancelLibraryPicker{}, m_session.snapshot());
        ASSERT_EQ(cancelled.status,
                  library::LibraryPickerTransitionStatus::RequestIssued);
        ASSERT_TRUE(cancelled.request.has_value());
        const auto* restore =
            std::get_if<library::RestoreLibraryContextRequest>(&*cancelled.request);
        ASSERT_NE(restore, nullptr);

        const auto returned = m_workshop->dispatch(
            workshop::ReturnFromLibraryIntent{
                workshop::ExperienceMode::Guided,
                restore->expectedGeneration});
        ASSERT_TRUE(returned.accepted());
        const auto completed = picker.dispatch(
            library::CompleteLibraryRestore{restore->token, true},
            m_session.snapshot());
        EXPECT_EQ(completed.status, library::LibraryPickerTransitionStatus::Applied);
        EXPECT_FALSE(completed.snapshot.active);
    }

    std::optional<dw::i64> createDirectCarveOperation(
        const dw::ProjectOpenItem& model) {
        dw::ProjectOpenItem operation;
        operation.itemType = dw::ProjectOpenItemType::Operation;
        operation.sourceTable = "direct_carve";
        operation.sourceKey =
            "direct_carve:model_item:" + std::to_string(model.id);
        operation.parentItemId = model.id;
        operation.status = dw::ProjectOpenItemStatus::Planned;
        operation.displayName = "Direct Carve: River Sign";
        operation.intentJson = nlohmann::json{
            {"operation_kind", "direct_carve"},
            {"model_name", "River Sign"},
        }.dump();
        operation.snapshotJson = R"({"setup":{"model_loaded":false}})";
        return m_projects->upsertCurrentOpenItem(std::move(operation));
    }

    bool persistPreparedOperation(const carve::PrepareCarvePin& pin) {
        auto operation =
            m_projects->findOpenItem(pin.operationItem().item.value);
        if (!operation || operation->projectId != pin.project().value ||
            operation->parentItemId != pin.modelItem().item.value) {
            return false;
        }
        operation->status = dw::ProjectOpenItemStatus::Ready;
        operation->snapshotJson = nlohmann::json{
            {"setup",
             {{"model_loaded", true},
              {"material_selected", true},
              {"finishing_tool_selected", true},
              {"toolpath_generated", true}}},
        }.dump();
        return m_projects->updateOpenItem(std::move(*operation));
    }

    static carve::PreparationReadinessSnapshot designReady(
        const carve::PrepareCarvePin& pin,
        std::uint64_t revision) {
        carve::PreparationReadinessSnapshot facts(
            pin, carve::PreparationEditRevision{revision});
        facts.modelLoaded = carve::PreparationEvidence::Satisfied;
        facts.modelFitsMachine = carve::PreparationEvidence::Satisfied;
        facts.materialSelected = carve::PreparationEvidence::Unsatisfied;
        facts.blankSpecified = carve::PreparationEvidence::Unsatisfied;
        facts.modelFitsBlank = carve::PreparationEvidence::Unsatisfied;
        facts.finishingToolSelected = carve::PreparationEvidence::Unsatisfied;
        facts.toolSetupConfirmed = carve::PreparationEvidence::Unsatisfied;
        facts.toolpathGenerated = carve::PreparationEvidence::Unsatisfied;
        facts.toolpathFresh = carve::PreparationEvidence::Unsatisfied;
        facts.hasUnsavedChanges = true;
        return facts;
    }

    static carve::PreparationReadinessSnapshot materialReady(
        const carve::PrepareCarvePin& pin,
        std::uint64_t revision) {
        auto facts = designReady(pin, revision);
        facts.materialSelected = carve::PreparationEvidence::Satisfied;
        facts.blankSpecified = carve::PreparationEvidence::Satisfied;
        facts.modelFitsBlank = carve::PreparationEvidence::Satisfied;
        return facts;
    }

    static carve::PreparationReadinessSnapshot toolReady(
        const carve::PrepareCarvePin& pin,
        std::uint64_t revision) {
        auto facts = materialReady(pin, revision);
        facts.finishingToolSelected = carve::PreparationEvidence::Satisfied;
        facts.toolSetupConfirmed = carve::PreparationEvidence::Satisfied;
        return facts;
    }

    dw::Path m_root;
    dw::Path m_previousProjectsDirectory;
    dw::Database m_database;
    workshop::ProjectSession m_session;
    std::unique_ptr<dw::ProjectManager> m_projects;
    std::unique_ptr<dw::LibraryManager> m_library;
    std::unique_ptr<dw::GCodeRepository> m_gcodes;
    std::unique_ptr<dw::LibraryWorkflowCoordinator> m_libraryWorkflow;
    std::unique_ptr<dw::ProjectSessionIntegration> m_integration;
    std::unique_ptr<workshop::ProjectWorkshopController> m_workshop;
};

TEST_F(ProjectCenteredWorkshopEndToEndTest,
       CanonicalStartAddPreviewPlanSaveAndRunStartPersistAcrossHomeAndReopen) {
    const dw::i64 primaryId = addRiverSignFixture(
        "river_sign_primary.stl", "River Sign Primary", "river-sign-primary-e2e");
    const dw::i64 previewId = addRiverSignFixture(
        "river_sign_preview_only.stl", "River Sign Preview", "river-sign-preview-e2e");
    const dw::i64 alternateId = addRiverSignFixture(
        "river_sign_alternate.stl", "River Sign Alternate", "river-sign-alternate-e2e");
    const workshop::LibraryItemRef primary{
        workshop::LibraryItemKind::Model, workshop::LibraryItemId(primaryId)};
    const workshop::LibraryItemRef alternate{
        workshop::LibraryItemKind::Model, workshop::LibraryItemId(alternateId)};
    const workshop::LibraryItemRef preview{
        workshop::LibraryItemKind::Model, workshop::LibraryItemId(previewId)};
    auto& picker = m_libraryWorkflow->picker();

    // T1: the production picker request starts River Sign with Primary.
    ASSERT_EQ(picker.dispatch(
                  library::BeginLibraryPicker{
                      library::LibraryPickerPurpose::StartProject, {}, {}},
                  m_session.snapshot()).status,
              library::LibraryPickerTransitionStatus::Applied);
    ASSERT_TRUE(m_workshop->dispatch(
                    workshop::NavigateWorkshopIntent{
                        workshop::ExperienceMode::Guided,
                        m_session.snapshot().generation,
                        workshop::WorkshopRoute::DesignLibrary}).accepted());
    ASSERT_EQ(picker.dispatch(
                  library::ReplaceLibrarySelection{{primary}}, m_session.snapshot()).status,
              library::LibraryPickerTransitionStatus::Applied);
    const auto startIssued = picker.dispatch(
        library::ConfirmLibrarySelection{}, m_session.snapshot());
    ASSERT_TRUE(startIssued.request.has_value());
    const auto* startRequest =
        std::get_if<library::StartProjectWithLibraryItemRequest>(&*startIssued.request);
    ASSERT_NE(startRequest, nullptr);
    ASSERT_TRUE(sameLibraryItem(startRequest->item, primary));

    dw::NamedProjectCreationService creation(*m_projects);
    const auto prepared = creation.prepare("  River Sign  ");
    ASSERT_TRUE(prepared.prepared());
    ASSERT_TRUE(m_integration->activateProject(prepared.project).committed());
    ASSERT_EQ(creation.finish(prepared.token, true),
              dw::NamedProjectFinishStatus::Published);
    ASSERT_NE(m_projects->currentProject(), nullptr);
    const workshop::ProjectId riverProject{m_projects->currentProject()->id()};
    const auto primaryAdded = m_libraryWorkflow->membership().ensure(
        {riverProject.value, {{dw::ProjectAssetKind::Model, primaryId}}});
    ASSERT_EQ(primaryAdded.status, dw::ProjectAssetMembershipStatus::Applied);
    ASSERT_EQ(picker.dispatch(
                  library::CompleteStartProject{startRequest->token, riverProject},
                  m_session.snapshot()).status,
              library::LibraryPickerTransitionStatus::Applied);
    EXPECT_FALSE(picker.snapshot().active);
    EXPECT_EQ(m_session.snapshot().activeProject, riverProject);
    EXPECT_EQ(m_projects->currentProject()->name(), "River Sign");
    EXPECT_FALSE(m_projects->currentProject()->isTemporary());

    // T2: a project with a chosen model rejects a second model without changing
    // the database or project directory.
    beginAddToProject(riverProject, m_libraryWorkflow->durableMembership(riverProject));
    ASSERT_EQ(picker.dispatch(
                  library::ReplaceLibrarySelection{{alternate}}, m_session.snapshot()).status,
              library::LibraryPickerTransitionStatus::Applied);
    const auto choiceBlocked = picker.dispatch(
        library::ConfirmLibrarySelection{}, m_session.snapshot());
    EXPECT_EQ(choiceBlocked.status,
              library::LibraryPickerTransitionStatus::Rejected);
    EXPECT_EQ(choiceBlocked.reason,
              library::LibraryPickerTransitionReason::ProjectAlreadyHasDesign);
    EXPECT_FALSE(choiceBlocked.request.has_value());
    const auto manifestBefore = dw::file::readText(
        m_projects->currentProject()->filePath() / "project.json");
    ASSERT_TRUE(manifestBefore.has_value());
    const dw::ProjectAssetMembershipRequest membershipRequest{
        riverProject.value, {{dw::ProjectAssetKind::Model, alternateId}}};
    const auto rejected = m_libraryWorkflow->membership().ensure(membershipRequest);
    EXPECT_EQ(rejected.status, dw::ProjectAssetMembershipStatus::Rejected);
    EXPECT_EQ(rejected.failure,
              dw::ProjectAssetMembershipFailure::ModelLimitExceeded);
    EXPECT_EQ(dw::file::readText(
                  m_projects->currentProject()->filePath() / "project.json").value_or(""),
              *manifestBefore);
    returnFromPicker();

    dw::ProjectRepository repository(m_database);
    EXPECT_TRUE(repository.hasModel(riverProject.value, primaryId));
    EXPECT_FALSE(repository.hasModel(riverProject.value, alternateId));

    // Preview is an isolated overlay: it neither adds membership nor loses the
    // exact route the picker must return to.
    beginAddToProject(riverProject, m_libraryWorkflow->durableMembership(riverProject));
    const auto previewSelected = picker.dispatch(
        library::ReplaceLibrarySelection{{preview}}, m_session.snapshot());
    ASSERT_EQ(previewSelected.snapshot.selectedItems.size(), 1U);
    EXPECT_TRUE(sameLibraryItem(previewSelected.snapshot.selectedItems.front(), preview));
    const auto previewIssued = picker.dispatch(
        library::RequestLibraryPreview{preview}, m_session.snapshot());
    ASSERT_TRUE(previewIssued.request.has_value());
    const auto* previewRequest =
        std::get_if<library::PreviewLibraryItemRequest>(&*previewIssued.request);
    ASSERT_NE(previewRequest, nullptr);
    EXPECT_TRUE(dw::library_workflow_adapter::previewStillCurrent(
        m_libraryWorkflow.get(), &m_session, previewRequest->token, preview));
    const auto presented = m_workshop->dispatch(
        workshop::PreviewLibraryItemIntent{
            workshop::ExperienceMode::Guided,
            m_session.snapshot().generation,
            preview});
    ASSERT_TRUE(presented.accepted());
    const auto previewCompleted = picker.dispatch(
        library::CompleteLibraryPreview{previewRequest->token, true},
        m_session.snapshot());
    ASSERT_TRUE(previewCompleted.snapshot.previewItem.has_value());
    EXPECT_TRUE(sameLibraryItem(*previewCompleted.snapshot.previewItem, preview));
    EXPECT_EQ(m_libraryWorkflow->durableMembership(riverProject).size(), 1U);
    EXPECT_FALSE(repository.hasModel(riverProject.value, previewId));
    returnFromPicker();
    EXPECT_EQ(m_session.snapshot().route, workshop::WorkshopRoute::Project);
    EXPECT_EQ(m_session.snapshot().activeProject, riverProject);

    auto modelItem = m_projects->findOpenItemBySource("models", primaryId);
    ASSERT_TRUE(modelItem.has_value());
    const workshop::ProjectItemRef modelRef{
        riverProject, workshop::ProjectItemId(modelItem->id)};
    const auto modelSelected = m_workshop->dispatch(
        workshop::SelectProjectItemIntent{
            workshop::ExperienceMode::Guided,
            m_session.snapshot().generation,
            modelRef});
    ASSERT_TRUE(modelSelected.accepted());
    ASSERT_EQ(modelSelected.context.activeProjectItem, modelRef);

    // A request pinned to River Sign cannot be redirected into another active
    // project, and the failed attempt writes neither project.
    auto otherProject = m_projects->create("Other Sign");
    ASSERT_NE(otherProject, nullptr);
    ASSERT_TRUE(m_projects->save(*otherProject));
    ASSERT_TRUE(m_integration->activateProject(otherProject).committed());
    const auto otherManifest =
        dw::file::readText(otherProject->filePath() / "project.json");
    ASSERT_TRUE(otherManifest.has_value());
    const auto crossProject = m_libraryWorkflow->membership().ensure(
        {riverProject.value,
         {{dw::ProjectAssetKind::Model, previewId}}});
    EXPECT_EQ(crossProject.status, dw::ProjectAssetMembershipStatus::Rejected);
    EXPECT_EQ(crossProject.failure, dw::ProjectAssetMembershipFailure::ProjectMismatch);
    EXPECT_FALSE(repository.hasModel(riverProject.value, previewId));
    EXPECT_FALSE(repository.hasModel(otherProject->id(), previewId));
    EXPECT_EQ(dw::file::readText(otherProject->filePath() / "project.json").value_or(""),
              *otherManifest);
    ASSERT_TRUE(m_integration->activateProject(
        m_projects->open(riverProject.value)).committed());

    modelItem = m_projects->findOpenItemBySource("models", primaryId);
    ASSERT_TRUE(modelItem.has_value());
    ASSERT_EQ(modelItem->id, modelRef.item.value);
    const auto itemsBeforePreparation = m_projects->currentOpenItems();
    const auto exactModel = std::find_if(
        itemsBeforePreparation.begin(), itemsBeforePreparation.end(),
        [&modelRef](const dw::ProjectOpenItem& item) {
            return item.id == modelRef.item.value &&
                   item.projectId == modelRef.project.value;
        });
    ASSERT_NE(exactModel, itemsBeforePreparation.end());
    ASSERT_NE(exactModel->status, dw::ProjectOpenItemStatus::Missing);
    ASSERT_NE(exactModel->status, dw::ProjectOpenItemStatus::Stale);
    ASSERT_EQ(dw::resolvePrepareCarvePin(
                  riverProject,
                  modelRef,
                  carve::PreparationToken{500},
                  carve::PreparationRevision{m_session.snapshot().generation.value},
                  itemsBeforePreparation)
                  .status,
              dw::PrepareCarveAdapterStatus::OperationRequired);
    const auto operationId = createDirectCarveOperation(*modelItem);
    ASSERT_TRUE(operationId.has_value());
    const workshop::ProjectItemRef operationRef{
        riverProject, workshop::ProjectItemId(*operationId)};
    const auto operationSelected = m_workshop->dispatch(
        workshop::SelectProjectItemIntent{
            workshop::ExperienceMode::Guided,
            m_session.snapshot().generation,
            operationRef});
    ASSERT_TRUE(operationSelected.accepted());
    ASSERT_EQ(operationSelected.context.activeProjectItem, operationRef);

    const auto pinResult = dw::resolvePrepareCarvePin(
        riverProject,
        operationRef,
        carve::PreparationToken{501},
        carve::PreparationRevision{m_session.snapshot().generation.value},
        m_projects->currentOpenItems());
    ASSERT_EQ(pinResult.status, dw::PrepareCarveAdapterStatus::Ready);
    ASSERT_TRUE(pinResult.pin.has_value());
    const carve::PrepareCarvePin pin = *pinResult.pin;
    EXPECT_EQ(pin.project(), riverProject);
    EXPECT_EQ(pin.modelItem(), modelRef);
    EXPECT_TRUE(sameLibraryItem(pin.modelSource(), primary));
    EXPECT_EQ(pin.operationItem(), operationRef);

    carve::PrepareCarveFlow preparation;
    auto transition = preparation.dispatch(
        carve::BeginPreparation{designReady(pin, 1)});
    ASSERT_EQ(transition.snapshot.activeStage,
              carve::PreparationStageId::DesignAndSize);
    transition = preparation.dispatch(carve::ContinuePreparation{});
    ASSERT_EQ(transition.snapshot.activeStage,
              carve::PreparationStageId::MaterialAndBlank);
    ASSERT_EQ(preparation.dispatch(
                  carve::RefreshPreparation{materialReady(pin, 2)})
                  .status,
              carve::PrepareCarveTransitionStatus::Applied);
    transition = preparation.dispatch(carve::ContinuePreparation{});
    ASSERT_EQ(transition.snapshot.activeStage,
              carve::PreparationStageId::ChooseTool);
    ASSERT_EQ(preparation.dispatch(
                  carve::RefreshPreparation{toolReady(pin, 3)})
                  .status,
              carve::PrepareCarveTransitionStatus::Applied);
    transition = preparation.dispatch(carve::ContinuePreparation{});
    ASSERT_EQ(transition.snapshot.activeStage,
              carve::PreparationStageId::CarvePreview);
    const auto previewGeneration = preparation.dispatch(
        carve::GeneratePreparationPreview{});
    ASSERT_EQ(previewGeneration.status,
              carve::PrepareCarveTransitionStatus::EffectIssued);
    const auto* generationRequest = std::get_if<carve::PreparationPreviewRequest>(
        &*previewGeneration.effect);
    ASSERT_NE(generationRequest, nullptr);
    EXPECT_EQ(generationRequest->readiness.pin(), pin);
    ASSERT_EQ(preparation.dispatch(
                  carve::CompletePreparationPreview{
                      pin, carve::PreparationEditRevision{3}, true})
                  .status,
              carve::PrepareCarveTransitionStatus::Applied);
    const auto ready = preparation.dispatch(carve::ContinuePreparation{});
    ASSERT_TRUE(ready.effect.has_value());
    const auto* readyEffect = std::get_if<carve::PreparationReady>(&*ready.effect);
    ASSERT_NE(readyEffect, nullptr);
    EXPECT_EQ(readyEffect->readiness.pin(), pin);

    ASSERT_TRUE(m_session.dispatch(
                    {{workshop::SetPreparationLock{true}}, std::nullopt})
                    .accepted());
    const auto save = preparation.dispatch(carve::SavePreparation{});
    ASSERT_EQ(save.status, carve::PrepareCarveTransitionStatus::EffectIssued);
    const auto* saveRequest =
        std::get_if<carve::PreparationSaveRequest>(&*save.effect);
    ASSERT_NE(saveRequest, nullptr);
    ASSERT_TRUE(persistPreparedOperation(saveRequest->readiness.pin()));
    ASSERT_EQ(preparation.dispatch(
                  carve::CompletePreparationSave{
                      pin, saveRequest->readiness.editRevision(), true})
                  .status,
              carve::PrepareCarveTransitionStatus::Applied);
    EXPECT_FALSE(preparation.snapshot().readiness->hasUnsavedChanges);
    ASSERT_TRUE(m_session.dispatch(
                    {{workshop::SetPreparationLock{false}}, std::nullopt})
                    .accepted());
    ASSERT_TRUE(m_projects->save(*m_projects->currentProject()));

    // Save the reviewed plan's exact G-code identity, prove all preflight
    // evidence is bound to it, and start only through the protected Run contract.
    const dw::Path gcodePath = m_root / "river_sign_ready.nc";
    ASSERT_TRUE(dw::file::writeText(
        gcodePath, "G21\nG90\nG0 X0 Y0 Z5\nG1 Z-1 F100\nG1 X10 F300\nM30\n"));
    const std::string fingerprint = dw::hash::computeFile(gcodePath);
    ASSERT_FALSE(fingerprint.empty());
    dw::GCodeRecord gcode;
    gcode.name = "River Sign Ready";
    gcode.hash = fingerprint;
    gcode.filePath = gcodePath;
    gcode.fileSize = dw::file::fileSize(gcodePath);
    const auto gcodeId = m_gcodes->insert(gcode);
    ASSERT_TRUE(gcodeId.has_value());
    ASSERT_EQ(m_libraryWorkflow->membership().ensure(
                  {riverProject.value,
                   {{dw::ProjectAssetKind::GCode, *gcodeId}}}).status,
              dw::ProjectAssetMembershipStatus::Applied);
    auto gcodeItem = m_projects->findOpenItemBySource("gcode_files", *gcodeId);
    ASSERT_TRUE(gcodeItem.has_value());
    gcodeItem->parentItemId = operationRef.item.value;
    gcodeItem->status = dw::ProjectOpenItemStatus::Ready;
    gcodeItem->intentJson = R"({"role":"generated_direct_carve_program"})";
    gcodeItem->snapshotJson = nlohmann::json{
        {"hash", fingerprint}, {"file_path", gcodePath.string()}}.dump();
    ASSERT_TRUE(m_projects->updateOpenItem(*gcodeItem));
    const workshop::ProjectItemRef gcodeRef{
        riverProject, workshop::ProjectItemId(gcodeItem->id)};

    const run::ToolpathIdentity toolpath(
        operationRef, gcodeRef, run::ToolpathRevision{4}, fingerprint);
    const run::RunSetupIdentity setup(pin, toolpath);
    const run::RunPackage package(
        run::RunIdentity(workshop::RunId{700}, setup),
        run::RunPreflightSnapshot(
            setup, run::PreflightRevision{1}, run::RunPreflightFacts::allSatisfied()));
    ASSERT_TRUE(package.valid());
    run::RunCoordinator coordinator;
    const auto started = coordinator.dispatch(run::StartRun{package});
    ASSERT_EQ(started.status, run::RunTransitionStatus::EffectsIssued);
    ASSERT_EQ(started.effects.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<run::AcquireRunLock>(started.effects[0]));
    EXPECT_TRUE(std::holds_alternative<run::StartStream>(started.effects[1]));
    ASSERT_TRUE(m_session.dispatch(
                    {workshop::BeginRun{
                         workshop::RunLockRef{package.identity().run(), operationRef}},
                     m_session.snapshot().generation}).accepted());
    EXPECT_EQ(m_session.snapshot().route, workshop::WorkshopRoute::RunCnc);
    const auto runFinished = coordinator.dispatch(
        run::CompleteRun{package.identity(), run::RunEventSequence{1}});
    ASSERT_EQ(runFinished.status, run::RunTransitionStatus::EffectsIssued);
    ASSERT_EQ(runFinished.effects.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<run::ReleaseRunLock>(
        runFinished.effects.front()));
    ASSERT_TRUE(m_session.dispatch(
                    {workshop::EndRun{package.identity().run()},
                     m_session.snapshot().generation}).accepted());
    EXPECT_EQ(m_session.snapshot().route, workshop::WorkshopRoute::Project);
    ASSERT_TRUE(m_projects->save(*m_projects->currentProject()));

    // Application-exit close keeps the exact bookmark. A new session restores
    // it through storage validation, project activation, and item activation.
    MemoryResumeStore store;
    workshop::ProjectResumeCoordinator remember(store, {});
    ASSERT_TRUE(remember.rememberProject(riverProject));
    ASSERT_TRUE(remember.rememberItem(operationRef));
    ASSERT_TRUE(m_integration->closeProject().committed());
    ASSERT_TRUE(remember.completeClose(
        workshop::ProjectClosePurpose::ApplicationExit));
    EXPECT_FALSE(m_session.snapshot().activeProject.has_value());
    EXPECT_EQ(m_session.snapshot().route, workshop::WorkshopRoute::Home);

    workshop::ProjectSession restartedSession;
    dw::ProjectSessionIntegration restartedIntegration(
        restartedSession, *m_projects);
    workshop::ProjectWorkshopController restartedWorkshop(
        restartedSession, true);
    workshop::ProjectResumeCallbacks callbacks;
    callbacks.inspectProject = [this](const workshop::ProjectResumeBookmark& bookmark) {
        return m_projects->validateProjectStorage(bookmark.project.value) ==
                       dw::ProjectStorageValidationStatus::Ready
                   ? workshop::ResumeProjectStatus::Ready
                   : workshop::ResumeProjectStatus::InvalidStorage;
    };
    callbacks.activateProject = [this, &restartedIntegration](workshop::ProjectId project) {
        auto opened = m_projects->open(project.value);
        return opened && restartedIntegration.activateProject(std::move(opened)).committed()
                   ? workshop::ResumeActivationStatus::Applied
                   : workshop::ResumeActivationStatus::Rejected;
    };
    callbacks.inspectItem = [this](workshop::ProjectItemRef item) {
        const auto stored = m_projects->findOpenItem(item.item.value);
        if (!stored) return workshop::ResumeItemStatus::Missing;
        return stored->projectId == item.project.value
                   ? workshop::ResumeItemStatus::Ready
                   : workshop::ResumeItemStatus::ForeignProject;
    };
    callbacks.activateItem = [&restartedSession, &restartedWorkshop](
                                 workshop::ProjectItemRef item) {
        const auto selected = restartedWorkshop.dispatch(
            workshop::SelectProjectItemIntent{
                workshop::ExperienceMode::Guided,
                restartedSession.snapshot().generation,
                item});
        return selected.accepted() ? workshop::ResumeActivationStatus::Applied
                                   : workshop::ResumeActivationStatus::Rejected;
    };
    workshop::ProjectResumeCoordinator resume(store, std::move(callbacks));
    const auto restored = resume.restore();
    ASSERT_EQ(restored.status,
              workshop::ProjectResumeStatus::ProjectAndItemRestored);
    const auto restoredContext = restartedSession.snapshot();
    EXPECT_EQ(restoredContext.activeProject, riverProject);
    ASSERT_EQ(restoredContext.activeProjectItem, operationRef);
    EXPECT_EQ(restoredContext.route, workshop::WorkshopRoute::Project);

    const auto resumedPin = dw::resolvePrepareCarvePin(
        restoredContext.activeProject,
        *restoredContext.activeProjectItem,
        carve::PreparationToken{502},
        carve::PreparationRevision{restoredContext.generation.value},
        m_projects->currentOpenItems());
    ASSERT_EQ(resumedPin.status, dw::PrepareCarveAdapterStatus::Ready);
    ASSERT_TRUE(resumedPin.pin.has_value());
    EXPECT_EQ(resumedPin.pin->project(), pin.project());
    EXPECT_EQ(resumedPin.pin->modelItem(), pin.modelItem());
    EXPECT_TRUE(sameLibraryItem(resumedPin.pin->modelSource(), pin.modelSource()));
    EXPECT_EQ(resumedPin.pin->operationItem(), pin.operationItem());
    ASSERT_NE(m_projects->currentProject(), nullptr);
    EXPECT_TRUE(m_projects->currentProject()->hasModel(primaryId));
    EXPECT_FALSE(m_projects->currentProject()->hasModel(alternateId));
    EXPECT_FALSE(m_projects->currentProject()->hasModel(previewId));
    EXPECT_TRUE(m_gcodes->isInProject(riverProject.value, *gcodeId));
    const auto savedPlan = m_projects->findOpenItem(operationRef.item.value);
    ASSERT_TRUE(savedPlan.has_value());
    EXPECT_EQ(savedPlan->status, dw::ProjectOpenItemStatus::Ready);
    const auto savedPlanJson = nlohmann::json::parse(savedPlan->snapshotJson);
    EXPECT_TRUE(savedPlanJson["setup"]["material_selected"].get<bool>());
    EXPECT_TRUE(savedPlanJson["setup"]["finishing_tool_selected"].get<bool>());
    EXPECT_TRUE(savedPlanJson["setup"]["toolpath_generated"].get<bool>());
    const auto savedGCode = m_projects->findOpenItem(gcodeRef.item.value);
    ASSERT_TRUE(savedGCode.has_value());
    EXPECT_EQ(savedGCode->parentItemId, operationRef.item.value);
    EXPECT_EQ(nlohmann::json::parse(savedGCode->snapshotJson)["hash"], fingerprint);
    EXPECT_FALSE(repository.hasModel(otherProject->id(), primaryId));
    EXPECT_FALSE(repository.hasModel(otherProject->id(), alternateId));
}

} // namespace

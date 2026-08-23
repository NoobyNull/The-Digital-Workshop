#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "../workshop/workshop_contract.h"

namespace dw::project_plan {

enum class ItemKind {
    Model,
    Material,
    Stock,
    Tool,
    Operation,
    GCode,
    CutPlan,
    Cost,
    Job,
    Labor,
    Consumable,
    Zeroing,
};

enum class ItemState {
    Planned,
    Ready,
    Generated,
    Sent,
    Complete,
    Stale,
    Missing,
};

enum class Evidence {
    Unknown,
    Unsatisfied,
    Satisfied,
};

struct ItemSnapshot {
    workshop::ProjectItemRef ref;
    ItemKind kind = ItemKind::Model;
    ItemState state = ItemState::Planned;
    std::string label;
    std::optional<workshop::ProjectItemId> parent;
};

struct OperationFacts {
    workshop::ProjectItemRef operation;
    Evidence modelLoaded = Evidence::Unknown;
    Evidence modelFitsBlank = Evidence::Unknown;
    Evidence modelFitsMachine = Evidence::Unknown;
    Evidence materialSelected = Evidence::Unknown;
    Evidence blankSpecified = Evidence::Unknown;
    Evidence finishingToolSelected = Evidence::Unknown;
    Evidence toolSetupConfirmed = Evidence::Unknown;
    Evidence toolpathGenerated = Evidence::Unknown;
    Evidence toolpathFresh = Evidence::Unknown;
    Evidence machineConnected = Evidence::Unknown;
    Evidence machineIdle = Evidence::Unknown;
    Evidence machineAlarmClear = Evidence::Unknown;
    Evidence machineProfileConfigured = Evidence::Unknown;
    Evidence machineHomedOrSkipped = Evidence::Unknown;
    Evidence limitSwitchesClear = Evidence::Unknown;
    Evidence safeZVerified = Evidence::Unknown;
    Evidence zeroVerified = Evidence::Unknown;
    Evidence outlineCompletedOrSkipped = Evidence::Unknown;
    Evidence finalConfirmed = Evidence::Unknown;
};

enum class RunState {
    Running,
    Paused,
    Interrupted,
};

struct LiveRunSnapshot {
    workshop::ProjectItemRef program;
    std::optional<workshop::ProjectItemRef> operation;
    workshop::ProjectItemRef job;
    RunState state = RunState::Running;
};

struct ProjectPlanInput {
    workshop::ProjectId project;
    std::string projectName;
    std::vector<ItemSnapshot> items;
    std::optional<workshop::ProjectItemRef> focusedItem;
    std::vector<OperationFacts> operations;
    std::optional<OperationFacts> liveOperation;
    std::optional<LiveRunSnapshot> liveRun;
};

enum class StageId {
    DesignAndSize,
    MaterialAndBlank,
    ChooseTool,
    CarvePreview,
    MachineSetup,
    ReviewAndRun,
};

enum class StageState {
    Locked,
    Available,
    NeedsAttention,
    InProgress,
    Complete,
    NotApplicable,
};

enum class NodeRole {
    ActivateItem,
    RepairItem,
    Informational,
};

enum class BlockerCode {
    MissingRequirement,
    VerificationRequired,
    MissingItem,
    StaleItem,
    MissingParent,
    ParentCycle,
    DuplicateItem,
    ForeignItem,
    SnapshotMismatch,
    RunStateMismatch,
};

struct StageBlocker {
    StageId stage = StageId::DesignAndSize;
    BlockerCode code = BlockerCode::MissingRequirement;
    std::optional<workshop::ProjectItemRef> item;
    std::string explanation;
};

struct PlanNode {
    ItemSnapshot item;
    StageId primaryStage = StageId::DesignAndSize;
    NodeRole role = NodeRole::Informational;
    std::vector<workshop::ProjectItemId> children;
    std::vector<StageBlocker> blockers;
};

struct ProjectPlanStage {
    StageId id = StageId::DesignAndSize;
    std::string title;
    StageState state = StageState::Locked;
    std::vector<workshop::ProjectItemId> contributingItems;
    std::vector<StageBlocker> blockers;
};

enum class NextActionKind {
    None,
    MonitorRun,
    ReconcileRunState,
    RepairItem,
    AddDesignFromLibrary,
    OpenDesignAndSize,
    OpenMaterialAndBlank,
    OpenToolSelection,
    OpenCarvePreview,
    OpenMachineSetup,
    OpenReviewAndRun,
    ReviewInterruptedRun,
};

struct NextAction {
    NextActionKind kind = NextActionKind::None;
    StageId stage = StageId::DesignAndSize;
    std::optional<workshop::ProjectItemRef> target;
    std::string label;
    std::string explanation;
};

enum class BuildStatus {
    Ready,
    Disabled,
    InvalidProject,
};

struct ProjectPlan {
    workshop::ProjectId project;
    std::string projectName;
    BuildStatus status = BuildStatus::InvalidProject;
    std::vector<PlanNode> nodes;
    std::vector<workshop::ProjectItemId> roots;
    std::array<ProjectPlanStage, 6> stages;
    std::vector<StageBlocker> diagnostics;
    NextAction nextAction;
    bool branchComplete = false;
};

struct ProjectPlanBuilderOptions {
    bool enabled = true;
};

class ProjectPlanBuilder final {
  public:
    explicit ProjectPlanBuilder(ProjectPlanBuilderOptions options = {});

    [[nodiscard]] ProjectPlan build(const ProjectPlanInput& input) const;

  private:
    ProjectPlanBuilderOptions m_options;
};

} // namespace dw::project_plan

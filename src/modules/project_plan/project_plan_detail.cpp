#include "project_plan_detail.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace dw::project_plan::detail {

StageId stageFor(ItemKind kind) {
    switch (kind) {
    case ItemKind::Model: return StageId::DesignAndSize;
    case ItemKind::Material:
    case ItemKind::Stock:
    case ItemKind::CutPlan: return StageId::MaterialAndBlank;
    case ItemKind::Tool: return StageId::ChooseTool;
    case ItemKind::Operation:
    case ItemKind::GCode: return StageId::CarvePreview;
    case ItemKind::Zeroing: return StageId::MachineSetup;
    case ItemKind::Cost:
    case ItemKind::Job:
    case ItemKind::Labor:
    case ItemKind::Consumable: return StageId::ReviewAndRun;
    }
    return StageId::DesignAndSize;
}

bool usable(ItemState state) {
    return state != ItemState::Missing && state != ItemState::Stale;
}

bool actionable(ItemKind kind) {
    switch (kind) {
    case ItemKind::Model:
    case ItemKind::Material:
    case ItemKind::Operation:
    case ItemKind::GCode:
    case ItemKind::CutPlan:
    case ItemKind::Cost: return true;
    default: return false;
    }
}

bool gatesProgress(ItemKind kind) {
    switch (kind) {
    case ItemKind::Model:
    case ItemKind::Material:
    case ItemKind::Stock:
    case ItemKind::Tool:
    case ItemKind::Operation:
    case ItemKind::GCode:
    case ItemKind::Zeroing: return true;
    default: return false;
    }
}

std::array<ProjectPlanStage, 6> makeStages() {
    return {{{StageId::DesignAndSize, "Design & Size", StageState::Locked, {}, {}},
             {StageId::MaterialAndBlank, "Material & Blank", StageState::Locked, {}, {}},
             {StageId::ChooseTool, "Choose Tool", StageState::Locked, {}, {}},
             {StageId::CarvePreview, "Carve Preview", StageState::Locked, {}, {}},
             {StageId::MachineSetup, "Machine Setup", StageState::Locked, {}, {}},
             {StageId::ReviewAndRun, "Review & Run", StageState::Locked, {}, {}}}};
}

StageBlocker blocker(StageId stage,
                     BlockerCode code,
                     std::string explanation,
                     std::optional<ItemRef> item) {
    return {stage, code, std::move(item), std::move(explanation)};
}

void requireEvidence(ProjectPlanStage& stage,
                     Evidence evidence,
                     const char* requirement) {
    if (evidence == Evidence::Satisfied) return;
    const bool unknown = evidence == Evidence::Unknown;
    stage.blockers.push_back(blocker(
        stage.id,
        unknown ? BlockerCode::VerificationRequired : BlockerCode::MissingRequirement,
        std::string(unknown ? "Verify " : "Complete ") + requirement + "."));
}

bool evidenceComplete(const ProjectPlanStage& stage) {
    return stage.blockers.empty();
}

namespace {
Evidence overlay(Evidence stored, Evidence live) {
    return live == Evidence::Unknown ? stored : live;
}
} // namespace

OperationFacts mergeFacts(OperationFacts stored, const OperationFacts& live) {
#define DW_OVERLAY_FACT(field) stored.field = overlay(stored.field, live.field)
    DW_OVERLAY_FACT(modelLoaded);
    DW_OVERLAY_FACT(modelFitsBlank);
    DW_OVERLAY_FACT(modelFitsMachine);
    DW_OVERLAY_FACT(materialSelected);
    DW_OVERLAY_FACT(blankSpecified);
    DW_OVERLAY_FACT(finishingToolSelected);
    DW_OVERLAY_FACT(toolSetupConfirmed);
    DW_OVERLAY_FACT(toolpathGenerated);
    DW_OVERLAY_FACT(toolpathFresh);
    DW_OVERLAY_FACT(machineConnected);
    DW_OVERLAY_FACT(machineIdle);
    DW_OVERLAY_FACT(machineAlarmClear);
    DW_OVERLAY_FACT(machineProfileConfigured);
    DW_OVERLAY_FACT(machineHomedOrSkipped);
    DW_OVERLAY_FACT(limitSwitchesClear);
    DW_OVERLAY_FACT(safeZVerified);
    DW_OVERLAY_FACT(zeroVerified);
    DW_OVERLAY_FACT(outlineCompletedOrSkipped);
    DW_OVERLAY_FACT(finalConfirmed);
#undef DW_OVERLAY_FACT
    return stored;
}

std::optional<std::size_t> findItem(const std::vector<WorkingItem>& items, ItemId id) {
    auto it = std::lower_bound(items.begin(), items.end(), id.value,
                               [](const WorkingItem& item, std::int64_t value) {
                                   return item.item.ref.item.value < value;
                               });
    if (it == items.end() || it->item.ref.item != id) return std::nullopt;
    return static_cast<std::size_t>(it - items.begin());
}

std::optional<std::size_t> ancestorOfKind(const std::vector<WorkingItem>& items,
                                          std::size_t start,
                                          ItemKind kind) {
    auto current = std::optional<std::size_t>(start);
    while (current) {
        if (items[*current].item.kind == kind) return current;
        current = items[*current].parent;
    }
    return std::nullopt;
}

bool descendsFrom(const std::vector<WorkingItem>& items,
                  std::size_t item,
                  std::size_t ancestor) {
    auto current = std::optional<std::size_t>(item);
    while (current) {
        if (*current == ancestor) return true;
        current = items[*current].parent;
    }
    return false;
}

bool completedJobBelow(const std::vector<WorkingItem>& items, std::size_t branch) {
    return std::any_of(items.begin(), items.end(), [&](const WorkingItem& candidate) {
        const auto index = static_cast<std::size_t>(&candidate - items.data());
        return candidate.item.kind == ItemKind::Job &&
               candidate.item.state == ItemState::Complete &&
               descendsFrom(items, index, branch);
    });
}

std::optional<std::size_t> focusedBranch(const ProjectPlanInput& input,
                                         const std::vector<WorkingItem>& items) {
    if (!input.focusedItem || input.focusedItem->project != input.project) return std::nullopt;
    const auto focused = findItem(items, input.focusedItem->item);
    if (!focused) return std::nullopt;
    if (auto operation = ancestorOfKind(items, *focused, ItemKind::Operation)) return operation;
    return ancestorOfKind(items, *focused, ItemKind::GCode);
}

void setAction(NextAction& action,
               NextActionKind kind,
               StageId stage,
               std::string label,
               std::string explanation,
               std::optional<ItemRef> target) {
    action = {kind, stage, std::move(target), std::move(label), std::move(explanation)};
}

} // namespace dw::project_plan::detail

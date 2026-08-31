#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "project_plan.h"

namespace dw::project_plan::detail {

using ItemId = workshop::ProjectItemId;
using ItemRef = workshop::ProjectItemRef;

struct WorkingItem {
    ItemSnapshot item;
    std::optional<std::size_t> parent;
    std::vector<StageBlocker> blockers;
};

constexpr std::size_t stageIndex(StageId stage) {
    return static_cast<std::size_t>(stage);
}

[[nodiscard]] StageId stageFor(ItemKind kind);
[[nodiscard]] bool usable(ItemState state);
[[nodiscard]] bool actionable(ItemKind kind);
[[nodiscard]] bool gatesProgress(ItemKind kind);
[[nodiscard]] std::array<ProjectPlanStage, 6> makeStages();
[[nodiscard]] StageBlocker blocker(
    StageId stage,
    BlockerCode code,
    std::string explanation,
    std::optional<ItemRef> item = std::nullopt);
void requireEvidence(ProjectPlanStage& stage,
                     Evidence evidence,
                     const char* requirement);
[[nodiscard]] bool evidenceComplete(const ProjectPlanStage& stage);
[[nodiscard]] OperationFacts mergeFacts(OperationFacts stored,
                                        const OperationFacts& live);
[[nodiscard]] std::optional<std::size_t>
findItem(const std::vector<WorkingItem>& items, ItemId id);
[[nodiscard]] std::optional<std::size_t>
ancestorOfKind(const std::vector<WorkingItem>& items,
               std::size_t start,
               ItemKind kind);
[[nodiscard]] bool descendsFrom(const std::vector<WorkingItem>& items,
                               std::size_t item,
                               std::size_t ancestor);
[[nodiscard]] bool completedJobBelow(const std::vector<WorkingItem>& items,
                                     std::size_t branch);
[[nodiscard]] std::optional<std::size_t>
focusedBranch(const ProjectPlanInput& input,
              const std::vector<WorkingItem>& items);
void setAction(NextAction& action,
               NextActionKind kind,
               StageId stage,
               std::string label,
               std::string explanation,
               std::optional<ItemRef> target = std::nullopt);

} // namespace dw::project_plan::detail

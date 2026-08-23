#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "core/database/project_repository.h"
#include "modules/carve_preparation/preparation_identity.h"
#include "modules/project_plan/project_plan.h"

namespace dw {

[[nodiscard]] project_plan::ItemKind
toProjectPlanItemKind(ProjectOpenItemType type) noexcept;
[[nodiscard]] project_plan::ItemState
toProjectPlanItemState(ProjectOpenItemStatus status) noexcept;

[[nodiscard]] project_plan::ProjectPlanInput makeProjectPlanInput(
    workshop::ProjectId project,
    std::string_view projectName,
    const std::vector<ProjectOpenItem>& items,
    std::optional<workshop::ProjectItemRef> focusedItem = std::nullopt);

// Live facts for the operation being prepared. Carve-stage evidence stays
// absent (Evidence::Unknown) until the CAM rebuild supplies it again.
[[nodiscard]] project_plan::OperationFacts makeLiveProjectPlanOperationFacts(
    const carve_preparation::PrepareCarvePin& pin,
    bool blankSpecified);

} // namespace dw

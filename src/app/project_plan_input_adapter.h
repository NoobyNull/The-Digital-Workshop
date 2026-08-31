#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "core/database/project_repository.h"
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

} // namespace dw

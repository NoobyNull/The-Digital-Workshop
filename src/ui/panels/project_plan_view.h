#pragma once

#include <functional>
#include <optional>

#include "modules/project_plan/project_plan.h"

namespace dw {

struct ProjectPlanViewCallbacks {
    std::function<void(const project_plan::NextAction&)> onNextAction;
    std::function<void(workshop::ProjectItemRef)> onActivateItem;
};

void renderProjectPlanView(
    const project_plan::ProjectPlan& plan,
    std::optional<workshop::ProjectItemId> activeItem,
    const ProjectPlanViewCallbacks& callbacks);

} // namespace dw

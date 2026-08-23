#pragma once

#include <string>

#include "project_plan.h"

namespace dw::project_plan {

struct ContinueCardPresentation {
    bool actionVisible = false;
    std::string stageLabel;
    std::string actionLabel;
    std::string explanation;
};

[[nodiscard]] const char* stageStateLabel(StageState state) noexcept;
[[nodiscard]] const char* itemKindLabel(ItemKind kind) noexcept;
[[nodiscard]] const char* itemStateLabel(ItemState state) noexcept;
[[nodiscard]] const char* nodeRoleLabel(NodeRole role) noexcept;
[[nodiscard]] bool nextActionIsActionable(NextActionKind kind) noexcept;
[[nodiscard]] ContinueCardPresentation
buildContinueCardPresentation(const ProjectPlan& plan);

} // namespace dw::project_plan

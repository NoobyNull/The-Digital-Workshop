#pragma once

#include <optional>
#include <string>
#include <vector>

#include "modules/workshop/workshop_contract.h"

namespace dw {

// A project holds what a slicer project file holds: the designs placed in it,
// the CAM setups made from them, and the G-code and runs those setups produced.
enum class ProjectContentKind {
    Design,
    Setup,
    Material,
    Tool,
    GCode,
    Run,
};

// One row of the project contents tree. Rows arrive parents first; a row whose
// parent is absent from the list renders at the root.
struct ProjectContentRow {
    workshop::ProjectItemRef ref;
    std::optional<workshop::ProjectItemId> parent;
    ProjectContentKind kind = ProjectContentKind::Design;
    std::string label;
    std::string status;
    bool needsAttention = false;
};

using ProjectContents = std::vector<ProjectContentRow>;

[[nodiscard]] constexpr const char* projectContentKindLabel(ProjectContentKind kind) noexcept {
    switch (kind) {
    case ProjectContentKind::Design: return "Design";
    case ProjectContentKind::Setup: return "CAM Setup";
    case ProjectContentKind::Material: return "Material";
    case ProjectContentKind::Tool: return "Tool";
    case ProjectContentKind::GCode: return "G-code";
    case ProjectContentKind::Run: return "Run";
    }
    return "Item";
}

} // namespace dw

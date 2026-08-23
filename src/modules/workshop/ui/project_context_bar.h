#pragma once

#include <functional>
#include <optional>

#include "modules/workshop/ui/project_context_bar_model.h"

namespace dw {

// Presentation-only shell for the immutable workshop snapshot. It emits one
// navigation intent and owns no project, repository, or machine state.
class ProjectContextBar final {
  public:
    using BackToProjectCallback = std::function<void()>;

    void setSnapshot(workshop::ProjectShellSnapshot snapshot);
    void setBackToProjectCallback(BackToProjectCallback callback);

    [[nodiscard]] float height() const;
    void render();

  private:
    std::optional<workshop::ProjectShellSnapshot> m_snapshot;
    BackToProjectCallback m_backToProject;
};

} // namespace dw

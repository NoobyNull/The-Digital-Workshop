#pragma once

#include <optional>
#include <string>
#include <variant>

#include "experience_router.h"

namespace dw::workshop {

struct ProjectDisplayFacts {
    std::optional<std::string> projectLabel;
    std::optional<std::string> itemLabel;
    std::optional<std::string> previewLabel;
};

struct MachineStatusSnapshot {
    std::string label;
    bool connected = false;
    bool running = false;
};

class ProjectShellSnapshot final {
  public:
    ProjectShellSnapshot(WorkshopContextSnapshot context,
                         ProjectDisplayFacts displayFacts,
                         MachineStatusSnapshot machineStatus);

    [[nodiscard]] const WorkshopContextSnapshot& context() const noexcept;
    [[nodiscard]] const ProjectDisplayFacts& displayFacts() const noexcept;
    [[nodiscard]] const MachineStatusSnapshot& machineStatus() const noexcept;

  private:
    WorkshopContextSnapshot m_context;
    ProjectDisplayFacts m_displayFacts;
    MachineStatusSnapshot m_machineStatus;
};

struct BackToProjectIntent {
    ExperienceMode source = ExperienceMode::Guided;
    ContextGeneration expectedGeneration;
};

struct NavigateWorkshopIntent {
    ExperienceMode source = ExperienceMode::Guided;
    ContextGeneration expectedGeneration;
    WorkshopRoute route = WorkshopRoute::Home;
};

struct ReturnFromLibraryIntent {
    ExperienceMode source = ExperienceMode::Guided;
    ContextGeneration expectedGeneration;
};

struct SelectProjectItemIntent {
    ExperienceMode source = ExperienceMode::Guided;
    ContextGeneration expectedGeneration;
    ProjectItemRef item;
};

struct ClearProjectItemIntent {
    ExperienceMode source = ExperienceMode::Guided;
    ContextGeneration expectedGeneration;
};

struct PreviewLibraryItemIntent {
    ExperienceMode source = ExperienceMode::Guided;
    ContextGeneration expectedGeneration;
    LibraryItemRef item;
};

using ProjectWorkshopIntent = std::variant<BackToProjectIntent,
                                           NavigateWorkshopIntent,
                                           ReturnFromLibraryIntent,
                                           SelectProjectItemIntent,
                                           ClearProjectItemIntent,
                                           PreviewLibraryItemIntent>;

class ProjectWorkshopController final {
  public:
    ProjectWorkshopController(WorkshopCommandTarget& target, bool guidedEnabled) noexcept;

    void setGuidedEnabled(bool enabled) noexcept;
    [[nodiscard]] bool guidedEnabled() const noexcept;

    [[nodiscard]] ProjectShellSnapshot snapshot(const ProjectDisplayFacts& displayFacts,
                                                const MachineStatusSnapshot& machineStatus) const;

    WorkshopTransition dispatch(const ProjectWorkshopIntent& intent);

  private:
    WorkshopTransition dispatchIntent(const BackToProjectIntent& intent);
    WorkshopTransition dispatchIntent(const NavigateWorkshopIntent& intent);
    WorkshopTransition dispatchIntent(const ReturnFromLibraryIntent& intent);
    WorkshopTransition dispatchIntent(const SelectProjectItemIntent& intent);
    WorkshopTransition dispatchIntent(const ClearProjectItemIntent& intent);
    WorkshopTransition dispatchIntent(const PreviewLibraryItemIntent& intent);

    WorkshopCommandTarget& m_target;
    ExperienceRouter m_router;
};

} // namespace dw::workshop

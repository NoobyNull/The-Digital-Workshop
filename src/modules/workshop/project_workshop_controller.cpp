#include "project_workshop_controller.h"

#include <utility>

namespace dw::workshop {

ProjectShellSnapshot::ProjectShellSnapshot(WorkshopContextSnapshot context,
                                           ProjectDisplayFacts displayFacts,
                                           MachineStatusSnapshot machineStatus)
    : m_context(std::move(context)), m_displayFacts(std::move(displayFacts)),
      m_machineStatus(std::move(machineStatus)) {}

const WorkshopContextSnapshot& ProjectShellSnapshot::context() const noexcept {
    return m_context;
}

const ProjectDisplayFacts& ProjectShellSnapshot::displayFacts() const noexcept {
    return m_displayFacts;
}

const MachineStatusSnapshot& ProjectShellSnapshot::machineStatus() const noexcept {
    return m_machineStatus;
}

ProjectWorkshopController::ProjectWorkshopController(WorkshopCommandTarget& target,
                                                     bool guidedEnabled) noexcept
    : m_target(target), m_router(target, guidedEnabled) {}

void ProjectWorkshopController::setGuidedEnabled(bool enabled) noexcept {
    m_router.setGuidedEnabled(enabled);
}

bool ProjectWorkshopController::guidedEnabled() const noexcept {
    return m_router.guidedEnabled();
}

ProjectShellSnapshot ProjectWorkshopController::snapshot(
    const ProjectDisplayFacts& displayFacts, const MachineStatusSnapshot& machineStatus) const {
    return {m_target.snapshot(), displayFacts, machineStatus};
}

WorkshopTransition ProjectWorkshopController::dispatch(const ProjectWorkshopIntent& intent) {
    return std::visit([this](const auto& typedIntent) { return dispatchIntent(typedIntent); },
                      intent);
}

WorkshopTransition ProjectWorkshopController::dispatchIntent(const BackToProjectIntent& intent) {
    WorkshopCommand command;
    command.expectedGeneration = intent.expectedGeneration;
    command.payload = NavigateTo{WorkshopRoute::Project};
    return m_router.dispatch(intent.source, command);
}

WorkshopTransition ProjectWorkshopController::dispatchIntent(const NavigateWorkshopIntent& intent) {
    WorkshopCommand command;
    command.payload = NavigateTo{intent.route};
    command.expectedGeneration = intent.expectedGeneration;
    return m_router.dispatch(intent.source, command);
}

WorkshopTransition ProjectWorkshopController::dispatchIntent(
    const ReturnFromLibraryIntent& intent) {
    WorkshopCommand command;
    command.payload = ReturnFromLibrary{};
    command.expectedGeneration = intent.expectedGeneration;
    return m_router.dispatch(intent.source, command);
}

WorkshopTransition ProjectWorkshopController::dispatchIntent(
    const SelectProjectItemIntent& intent) {
    WorkshopCommand command;
    command.payload = SelectProjectItem{intent.item};
    command.expectedGeneration = intent.expectedGeneration;
    return m_router.dispatch(intent.source, command);
}

WorkshopTransition ProjectWorkshopController::dispatchIntent(const ClearProjectItemIntent& intent) {
    WorkshopCommand command;
    command.payload = ClearProjectItem{};
    command.expectedGeneration = intent.expectedGeneration;
    return m_router.dispatch(intent.source, command);
}

WorkshopTransition ProjectWorkshopController::dispatchIntent(
    const PreviewLibraryItemIntent& intent) {
    if (!intent.item.valid()) {
        return {TransitionStatus::Rejected,
                TransitionReason::InvalidReference,
                m_target.snapshot()};
    }

    WorkshopCommand navigate;
    navigate.payload = NavigateTo{WorkshopRoute::DesignLibrary};
    navigate.expectedGeneration = intent.expectedGeneration;
    const auto navigation = m_router.dispatch(intent.source, navigate);
    if (!navigation.accepted())
        return navigation;

    WorkshopCommand preview;
    preview.payload = PreviewLibraryItem{intent.item};
    preview.expectedGeneration = navigation.context.generation;
    return m_router.dispatch(intent.source, preview);
}

} // namespace dw::workshop

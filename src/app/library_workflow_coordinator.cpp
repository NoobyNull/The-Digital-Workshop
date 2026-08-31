#include "library_workflow_coordinator.h"

#include "core/database/database.h"
#include "core/database/gcode_repository.h"
#include "core/library/library_manager.h"
#include "core/project/project.h"

namespace dw {

LibraryWorkflowCoordinator::LibraryWorkflowCoordinator(Database& database,
                                                       LibraryManager& library,
                                                       ProjectManager& projects,
                                                       GCodeRepository& gcodes)
    : m_projectRepository(database), m_gcodes(gcodes), m_membership(projects),
      m_deletion(library, m_projectRepository, gcodes) {}

std::vector<workshop::LibraryItemRef>
LibraryWorkflowCoordinator::durableMembership(workshop::ProjectId project) {
    std::vector<workshop::LibraryItemRef> items;
    if (!project.valid())
        return items;

    const auto modelIds = m_projectRepository.getModelIds(project.value);
    const auto gcodes = m_gcodes.findByProject(project.value);
    items.reserve(modelIds.size() + gcodes.size());
    for (const i64 modelId : modelIds) {
        items.push_back(
            {workshop::LibraryItemKind::Model, workshop::LibraryItemId(modelId)});
    }
    for (const auto& gcode : gcodes) {
        items.push_back(
            {workshop::LibraryItemKind::GCode, workshop::LibraryItemId(gcode.id)});
    }
    return items;
}

} // namespace dw

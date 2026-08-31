#pragma once

#include <vector>

#include "core/database/project_repository.h"
#include "core/library/library_source_deletion.h"
#include "core/project/project_asset_membership.h"
#include "modules/design_library/library_picker_flow.h"

namespace dw {

class Database;
class GCodeRepository;
class LibraryManager;
class ProjectManager;

// Application-owned composition root for the removable Design Library
// workflow. Policy remains in LibraryPickerFlow and durable mutation remains in
// the two core services; this object only owns their lifetimes and adapters.
class LibraryWorkflowCoordinator final {
  public:
    LibraryWorkflowCoordinator(Database& database,
                               LibraryManager& library,
                               ProjectManager& projects,
                               GCodeRepository& gcodes);

    [[nodiscard]] design_library::LibraryPickerFlow& picker() noexcept {
        return m_picker;
    }
    [[nodiscard]] const design_library::LibraryPickerFlow& picker() const noexcept {
        return m_picker;
    }
    [[nodiscard]] ProjectAssetMembershipService& membership() noexcept {
        return m_membership;
    }
    [[nodiscard]] LibrarySourceDeletionService& deletion() noexcept {
        return m_deletion;
    }

    [[nodiscard]] std::vector<workshop::LibraryItemRef>
    durableMembership(workshop::ProjectId project);

  private:
    ProjectRepository m_projectRepository;
    GCodeRepository& m_gcodes;
    design_library::LibraryPickerFlow m_picker;
    ProjectAssetMembershipService m_membership;
    LibrarySourceDeletionService m_deletion;
};

} // namespace dw

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/library/library_source_deletion.h"
#include "core/project/project_asset_membership.h"
#include "modules/design_library/library_picker_presentation.h"

namespace dw {

class LibraryWorkflowCoordinator;

namespace workshop {
class ProjectSession;
}

// Small, UI-free conversions and completion guards shared by the application
// composition units. Keeping them here prevents policy glue from growing back
// into Application or LibraryPanel.
namespace library_workflow_adapter {

[[nodiscard]] design_library::LibraryActionResult accepted(std::string message = {});
[[nodiscard]] design_library::LibraryActionResult pending();
[[nodiscard]] design_library::LibraryActionResult rejected(std::string message);

[[nodiscard]] bool sameLibraryItem(workshop::LibraryItemRef lhs,
                                   workshop::LibraryItemRef rhs) noexcept;
[[nodiscard]] bool flowAccepted(
    const design_library::LibraryPickerTransition& transition) noexcept;
[[nodiscard]] std::string pickerFailureMessage(
    design_library::LibraryPickerTransitionReason reason);

[[nodiscard]] std::optional<ProjectAssetRef> projectAsset(
    workshop::LibraryItemRef item);
[[nodiscard]] std::vector<workshop::LibraryItemRef> durableItems(
    const ProjectAssetMembershipResult& result);
[[nodiscard]] std::string membershipFailure(ProjectAssetMembershipFailure failure);

[[nodiscard]] bool previewStillCurrent(
    const LibraryWorkflowCoordinator* workflow,
    const workshop::ProjectSession* session,
    design_library::LibraryPickerRequestToken token,
    workshop::LibraryItemRef item);

[[nodiscard]] std::optional<LibrarySourceRef> deletionSource(
    workshop::LibraryItemRef item);
[[nodiscard]] std::string projectBlockMessage(
    const std::vector<LibrarySourceProjectRef>& projects);

} // namespace library_workflow_adapter
} // namespace dw

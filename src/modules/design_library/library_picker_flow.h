#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "modules/workshop/workshop_contract.h"

namespace dw::design_library {

enum class LibraryPickerPurpose {
    ManageLibrary,
    StartProject,
    AddToProject,
};

struct LibraryPickerRequestToken {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return value > 0; }

    friend constexpr bool operator==(LibraryPickerRequestToken lhs,
                                     LibraryPickerRequestToken rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(LibraryPickerRequestToken lhs,
                                     LibraryPickerRequestToken rhs) noexcept {
        return !(lhs == rhs);
    }
};

struct LibraryPickerSnapshot {
    bool active = false;
    LibraryPickerPurpose purpose = LibraryPickerPurpose::ManageLibrary;
    std::optional<workshop::ProjectId> activeProject;
    std::string activeProjectName;
    workshop::WorkshopRoute returnRoute = workshop::WorkshopRoute::Home;
    std::optional<workshop::ProjectItemRef> returnProjectItem;
    std::vector<workshop::LibraryItemRef> projectMembership;
    std::vector<workshop::LibraryItemRef> selectedItems;
    std::optional<workshop::LibraryItemRef> previewItem;
    std::optional<workshop::LibraryItemRef> pendingPreviewItem;
    std::optional<LibraryPickerRequestToken> pendingPreviewToken;
    std::vector<workshop::LibraryItemRef> pendingAddItems;
    std::optional<LibraryPickerRequestToken> pendingActionToken;
    bool startRequestPending = false;
    bool returnPending = false;
    std::optional<LibraryPickerRequestToken> pendingRestoreToken;
    std::optional<workshop::ContextGeneration> pendingRestoreGeneration;

    [[nodiscard]] bool isProjectMember(workshop::LibraryItemRef item) const noexcept;
    [[nodiscard]] bool isAddPending(workshop::LibraryItemRef item) const noexcept;
    [[nodiscard]] bool canConfirm() const noexcept;
    [[nodiscard]] std::string primaryActionLabel() const;
};

struct BeginLibraryPicker {
    LibraryPickerPurpose purpose = LibraryPickerPurpose::ManageLibrary;
    std::string activeProjectName;
    std::vector<workshop::LibraryItemRef> projectMembership;
};

struct ReplaceLibrarySelection {
    std::vector<workshop::LibraryItemRef> items;
};

struct RequestLibraryPreview {
    workshop::LibraryItemRef item;
};

struct CompleteLibraryPreview {
    LibraryPickerRequestToken token;
    bool presented = false;
};

struct OfferImportedLibraryItems {
    std::vector<workshop::LibraryItemRef> items;
};

struct ConfirmLibrarySelection {};

struct CompleteLibraryAdd {
    LibraryPickerRequestToken token;
    workshop::ProjectId project;
    std::vector<workshop::LibraryItemRef> addedItems;
};

struct CompleteStartProject {
    LibraryPickerRequestToken token;
    std::optional<workshop::ProjectId> createdProject;
};

struct CompleteLibraryRestore {
    LibraryPickerRequestToken token;
    bool restored = false;
};

struct CancelLibraryPicker {};

using LibraryPickerIntent = std::variant<BeginLibraryPicker,
                                         ReplaceLibrarySelection,
                                         RequestLibraryPreview,
                                         CompleteLibraryPreview,
                                         OfferImportedLibraryItems,
                                         ConfirmLibrarySelection,
                                         CompleteLibraryAdd,
                                         CompleteStartProject,
                                         CompleteLibraryRestore,
                                         CancelLibraryPicker>;

struct PreviewLibraryItemRequest {
    LibraryPickerRequestToken token;
    workshop::LibraryItemRef item;
};

struct StartProjectWithLibraryItemRequest {
    LibraryPickerRequestToken token;
    workshop::LibraryItemRef item;
};

// The executor must ensure these memberships rather than blindly inserting
// rows. The flow also suppresses duplicate requests while one is outstanding.
struct EnsureLibraryItemsInProjectRequest {
    LibraryPickerRequestToken token;
    workshop::ProjectId project;
    std::vector<workshop::LibraryItemRef> items;
};

struct RestoreLibraryContextRequest {
    LibraryPickerRequestToken token;
    workshop::ContextGeneration expectedGeneration;
    workshop::WorkshopRoute route = workshop::WorkshopRoute::Home;
    std::optional<workshop::ProjectId> project;
    std::optional<workshop::ProjectItemRef> projectItem;
};

using LibraryPickerRequest = std::variant<PreviewLibraryItemRequest,
                                          StartProjectWithLibraryItemRequest,
                                          EnsureLibraryItemsInProjectRequest,
                                          RestoreLibraryContextRequest>;

enum class LibraryPickerTransitionStatus {
    Applied,
    Unchanged,
    Rejected,
    RequestIssued,
};

enum class LibraryPickerTransitionReason {
    None,
    NotActive,
    AlreadyActive,
    InvalidContext,
    ContextChanged,
    PickerNotVisible,
    NoActiveProject,
    InvalidItem,
    ItemNotSelected,
    SelectionRequired,
    SingleModelRequired,
    ProjectAlreadyHasDesign,
    NoPrimaryAction,
    RequestPending,
    ProjectMismatch,
    AlreadyMember,
    WrongPurpose,
    StaleCompletion,
};

struct LibraryPickerTransition {
    LibraryPickerTransitionStatus status = LibraryPickerTransitionStatus::Unchanged;
    LibraryPickerTransitionReason reason = LibraryPickerTransitionReason::None;
    LibraryPickerSnapshot snapshot;
    std::optional<LibraryPickerRequest> request;
};

class LibraryPickerFlow final {
  public:
    [[nodiscard]] const LibraryPickerSnapshot& snapshot() const noexcept;

    LibraryPickerTransition dispatch(const LibraryPickerIntent& intent,
                                     const workshop::WorkshopContextSnapshot& projectSession);

  private:
    LibraryPickerTransition handle(const BeginLibraryPicker& intent,
                                   const workshop::WorkshopContextSnapshot& projectSession);
    LibraryPickerTransition handle(const ReplaceLibrarySelection& intent,
                                   const workshop::WorkshopContextSnapshot& projectSession);
    LibraryPickerTransition handle(const RequestLibraryPreview& intent,
                                   const workshop::WorkshopContextSnapshot& projectSession);
    LibraryPickerTransition handle(const CompleteLibraryPreview& intent,
                                   const workshop::WorkshopContextSnapshot& projectSession);
    LibraryPickerTransition handle(const OfferImportedLibraryItems& intent,
                                   const workshop::WorkshopContextSnapshot& projectSession);
    LibraryPickerTransition handle(const ConfirmLibrarySelection& intent,
                                   const workshop::WorkshopContextSnapshot& projectSession);
    LibraryPickerTransition handle(const CompleteLibraryAdd& intent,
                                   const workshop::WorkshopContextSnapshot& projectSession);
    LibraryPickerTransition handle(const CompleteStartProject& intent,
                                   const workshop::WorkshopContextSnapshot& projectSession);
    LibraryPickerTransition handle(const CompleteLibraryRestore& intent,
                                   const workshop::WorkshopContextSnapshot& projectSession);
    LibraryPickerTransition handle(const CancelLibraryPicker& intent,
                                   const workshop::WorkshopContextSnapshot& projectSession);

    [[nodiscard]] bool contextMatches(
        const workshop::WorkshopContextSnapshot& projectSession) const noexcept;
    [[nodiscard]] bool returnContextMatches(
        const workshop::WorkshopContextSnapshot& projectSession) const noexcept;
    [[nodiscard]] LibraryPickerRequestToken nextToken() noexcept;
    [[nodiscard]] LibraryPickerTransition transition(
        LibraryPickerTransitionStatus status,
        LibraryPickerTransitionReason reason = LibraryPickerTransitionReason::None) const;

    LibraryPickerSnapshot m_snapshot;
    std::uint64_t m_nextTokenValue = 1;
};

} // namespace dw::design_library

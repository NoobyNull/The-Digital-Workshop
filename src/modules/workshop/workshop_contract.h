#pragma once

#include <cstdint>
#include <optional>
#include <variant>

namespace dw::workshop {

template <typename Tag>
struct StrongId {
    std::int64_t value = 0;

    constexpr StrongId() noexcept = default;
    explicit constexpr StrongId(std::int64_t rawValue) noexcept : value(rawValue) {}

    [[nodiscard]] constexpr bool valid() const noexcept { return value > 0; }

    friend constexpr bool operator==(StrongId lhs, StrongId rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(StrongId lhs, StrongId rhs) noexcept {
        return !(lhs == rhs);
    }
};

struct ProjectIdTag {};
struct ProjectItemIdTag {};
struct LibraryItemIdTag {};
struct RunIdTag {};

using ProjectId = StrongId<ProjectIdTag>;
using ProjectItemId = StrongId<ProjectItemIdTag>;
using LibraryItemId = StrongId<LibraryItemIdTag>;
using RunId = StrongId<RunIdTag>;

struct ContextGeneration {
    std::uint64_t value = 0;

    friend constexpr bool operator==(ContextGeneration lhs,
                                     ContextGeneration rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(ContextGeneration lhs,
                                     ContextGeneration rhs) noexcept {
        return !(lhs == rhs);
    }
};

struct ConfirmationToken {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return value > 0; }

    friend constexpr bool operator==(ConfirmationToken lhs,
                                     ConfirmationToken rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(ConfirmationToken lhs,
                                     ConfirmationToken rhs) noexcept {
        return !(lhs == rhs);
    }
};

enum class LibraryItemKind {
    Model,
    GCode,
};

struct ProjectItemRef {
    ProjectId project;
    ProjectItemId item;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return project.valid() && item.valid();
    }

    friend constexpr bool operator==(ProjectItemRef lhs, ProjectItemRef rhs) noexcept {
        return lhs.project == rhs.project && lhs.item == rhs.item;
    }

    friend constexpr bool operator!=(ProjectItemRef lhs, ProjectItemRef rhs) noexcept {
        return !(lhs == rhs);
    }
};

struct LibraryItemRef {
    LibraryItemKind kind = LibraryItemKind::Model;
    LibraryItemId item;

    [[nodiscard]] constexpr bool valid() const noexcept { return item.valid(); }
};

struct RunLockRef {
    RunId run;
    std::optional<ProjectItemRef> projectOperation;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return run.valid() &&
               (!projectOperation.has_value() || projectOperation->valid());
    }

    [[nodiscard]] constexpr bool projectPinned() const noexcept {
        return projectOperation.has_value();
    }
};

// Prepare Carve remains inside Project. Advanced is an experience rendering the
// same context, not a second route or a second source of project truth.
enum class WorkshopRoute {
    Home,
    Project,
    DesignLibrary,
    RunCnc,
};

enum class SelectionOrigin {
    None,
    ProjectItem,
    LibraryPreview,
    StandaloneFile,
    ExternalRun,
};

enum class ExperienceMode {
    Guided,
    Advanced,
};

struct WorkshopContextSnapshot {
    std::optional<ProjectId> activeProject;
    std::optional<ProjectItemRef> activeProjectItem;
    std::optional<LibraryItemRef> libraryPreview;
    std::optional<RunLockRef> activeRun;
    std::optional<WorkshopRoute> libraryReturnRoute;
    std::optional<WorkshopRoute> runReturnRoute;
    WorkshopRoute route = WorkshopRoute::Home;
    SelectionOrigin origin = SelectionOrigin::None;
    ContextGeneration generation;
    bool projectDirty = false;
    bool preparationLocked = false;
    [[nodiscard]] bool runLocked() const noexcept { return activeRun.has_value(); }
};

struct ActivateProject {
    ProjectId project;
};

struct CloseProject {};

struct SelectProjectItem {
    ProjectItemRef item;
};

struct ClearProjectItem {};

struct NavigateTo {
    WorkshopRoute route = WorkshopRoute::Home;
};

struct PreviewLibraryItem {
    LibraryItemRef item;
};

struct ReturnFromLibrary {};

struct SetProjectDirty {
    bool dirty = false;
};

struct SetPreparationLock {
    bool locked = false;
};

struct BeginRun {
    RunLockRef run;
};

struct EndRun {
    RunId run;
};

enum class PendingTransitionResolution {
    Cancel,
    ChangesResolved,
};

struct ResolvePendingTransition {
    ConfirmationToken token;
    PendingTransitionResolution resolution = PendingTransitionResolution::Cancel;
};

using WorkshopCommandPayload = std::variant<ActivateProject,
                                            CloseProject,
                                            SelectProjectItem,
                                            ClearProjectItem,
                                            NavigateTo,
                                            PreviewLibraryItem,
                                            ReturnFromLibrary,
                                            SetProjectDirty,
                                            SetPreparationLock,
                                            BeginRun,
                                            EndRun,
                                            ResolvePendingTransition>;

struct WorkshopCommand {
    WorkshopCommandPayload payload = NavigateTo{};
    std::optional<ContextGeneration> expectedGeneration;
};

enum class TransitionStatus {
    Applied,
    Unchanged,
    ConfirmationRequired,
    Blocked,
    Rejected,
};

enum class TransitionReason {
    None,
    ExperienceDisabled,
    InvalidReference,
    NoActiveProject,
    ProjectMismatch,
    StaleGeneration,
    ActiveRun,
    UnsavedProject,
    UnsavedPreparation,
    PendingConfirmation,
    StaleConfirmation,
    RunMismatch,
    InvalidTransition,
};

struct PendingChanges {
    bool unsavedProject = false;
    bool unsavedPreparation = false;

    [[nodiscard]] constexpr bool any() const noexcept {
        return unsavedProject || unsavedPreparation;
    }
};

struct WorkshopTransition {
    TransitionStatus status = TransitionStatus::Unchanged;
    TransitionReason reason = TransitionReason::None;
    WorkshopContextSnapshot context;
    std::optional<ConfirmationToken> confirmation;
    PendingChanges pendingChanges;

    WorkshopTransition() = default;
    WorkshopTransition(TransitionStatus transitionStatus,
                       TransitionReason transitionReason,
                       const WorkshopContextSnapshot& snapshot)
        : status(transitionStatus), reason(transitionReason), context(snapshot) {}

    [[nodiscard]] bool accepted() const noexcept {
        return status == TransitionStatus::Applied ||
               status == TransitionStatus::Unchanged;
    }
};

} // namespace dw::workshop

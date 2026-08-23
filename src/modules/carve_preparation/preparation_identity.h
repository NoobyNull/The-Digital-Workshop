#pragma once

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "modules/workshop/workshop_contract.h"

namespace dw::carve_preparation {

struct PreparationToken {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return value > 0; }

    friend constexpr bool operator==(PreparationToken lhs, PreparationToken rhs) noexcept {
        return lhs.value == rhs.value;
    }
};

struct PreparationRevision {
    std::uint64_t value = 0;

    friend constexpr bool operator==(PreparationRevision lhs, PreparationRevision rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(PreparationRevision lhs, PreparationRevision rhs) noexcept {
        return !(lhs == rhs);
    }
};

// A pin has no mutators: once accepted, every asynchronous preparation result
// can carry this value instead of consulting whichever project is active later.
class PrepareCarvePin final {
  public:
    PrepareCarvePin(workshop::ProjectId project,
                    workshop::ProjectItemRef modelItem,
                    workshop::LibraryItemRef modelSource,
                    workshop::ProjectItemRef operationItem,
                    PreparationToken token,
                    PreparationRevision revision) noexcept;

    [[nodiscard]] workshop::ProjectId project() const noexcept;
    [[nodiscard]] workshop::ProjectItemRef modelItem() const noexcept;
    [[nodiscard]] workshop::LibraryItemRef modelSource() const noexcept;
    [[nodiscard]] workshop::ProjectItemRef operationItem() const noexcept;
    [[nodiscard]] PreparationToken token() const noexcept;
    [[nodiscard]] PreparationRevision revision() const noexcept;

    friend bool operator==(const PrepareCarvePin& lhs, const PrepareCarvePin& rhs) noexcept;

  private:
    workshop::ProjectId m_project;
    workshop::ProjectItemRef m_modelItem;
    workshop::LibraryItemRef m_modelSource;
    workshop::ProjectItemRef m_operationItem;
    PreparationToken m_token;
    PreparationRevision m_revision;
};

enum class PreparationItemKind {
    Model,
    Operation,
};

// The adapter supplies one immutable, project-scoped view of identity data.
// This module deliberately receives no repository from which it could refresh
// or silently substitute a different item during evaluation.
class PreparationItemSnapshot final {
  public:
    PreparationItemSnapshot(workshop::ProjectItemRef ref,
                            PreparationItemKind kind,
                            std::optional<workshop::ProjectItemId> parent = std::nullopt,
                            std::optional<workshop::LibraryItemRef> source = std::nullopt) noexcept;

    [[nodiscard]] workshop::ProjectItemRef ref() const noexcept;
    [[nodiscard]] PreparationItemKind kind() const noexcept;
    [[nodiscard]] std::optional<workshop::ProjectItemId> parent() const noexcept;
    [[nodiscard]] std::optional<workshop::LibraryItemRef> source() const noexcept;

  private:
    workshop::ProjectItemRef m_ref;
    PreparationItemKind m_kind;
    std::optional<workshop::ProjectItemId> m_parent;
    std::optional<workshop::LibraryItemRef> m_source;
};

class PreparationIdentitySnapshot final {
  public:
    PreparationIdentitySnapshot(std::optional<workshop::ProjectId> activeProject,
                                PreparationRevision revision,
                                std::vector<PreparationItemSnapshot> items);

    [[nodiscard]] std::optional<workshop::ProjectId> activeProject() const noexcept;
    [[nodiscard]] PreparationRevision revision() const noexcept;
    [[nodiscard]] const std::vector<PreparationItemSnapshot>& items() const noexcept;

  private:
    std::optional<workshop::ProjectId> m_activeProject;
    PreparationRevision m_revision;
    std::vector<PreparationItemSnapshot> m_items;
};

enum class PreparationIdentityStatus {
    Disabled,
    Ready,
    CreateProjectRequired,
    InvalidIdentity,
    StaleIdentity,
};

enum class PreparationIdentityIssue {
    None,
    ModuleDisabled,
    NoActiveProject,
    MissingPin,
    InvalidActiveProject,
    InvalidProject,
    InvalidModelItem,
    InvalidModelSource,
    InvalidOperationItem,
    InvalidToken,
    CrossProjectModel,
    CrossProjectOperation,
    ModelAndOperationAreSameItem,
    ActiveProjectChanged,
    RevisionChanged,
    InvalidSnapshotItem,
    ForeignSnapshotItem,
    DuplicateSnapshotItem,
    ModelMissing,
    OperationMissing,
    WrongModelKind,
    WrongOperationKind,
    ModelSourceChanged,
    OperationParentMismatch,
};

struct RequestProjectCreation {};

struct BeginPinnedPreparation {
    PrepareCarvePin pin;
};

// These are preparation-routing intents only. There is intentionally no
// persistence, project activation, output-write, or machine command variant.
using PreparationIdentityCommand = std::variant<RequestProjectCreation, BeginPinnedPreparation>;

struct PreparationIdentityDecision {
    PreparationIdentityStatus status = PreparationIdentityStatus::InvalidIdentity;
    PreparationIdentityIssue issue = PreparationIdentityIssue::MissingPin;
    std::optional<PreparationIdentityCommand> command;
};

class PreparationIdentityPolicy final {
  public:
    [[nodiscard]] static PreparationIdentityDecision evaluate(
        bool enabled,
        const std::optional<PrepareCarvePin>& proposedPin,
        const PreparationIdentitySnapshot& snapshot);
};

} // namespace dw::carve_preparation

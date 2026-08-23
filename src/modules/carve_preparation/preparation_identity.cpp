#include "preparation_identity.h"

#include <cstddef>
#include <utility>

namespace dw::carve_preparation {
namespace {

using Decision = PreparationIdentityDecision;
using Issue = PreparationIdentityIssue;
using Status = PreparationIdentityStatus;

bool sameLibraryItem(workshop::LibraryItemRef lhs, workshop::LibraryItemRef rhs) noexcept {
    return lhs.kind == rhs.kind && lhs.item == rhs.item;
}

Decision noCommand(Status status, Issue issue) {
    return Decision{status, issue, std::nullopt};
}

Decision createProjectRequired() {
    return Decision{Status::CreateProjectRequired,
                    Issue::NoActiveProject,
                    PreparationIdentityCommand{RequestProjectCreation{}}};
}

Decision ready(const PrepareCarvePin& pin) {
    return Decision{Status::Ready,
                    Issue::None,
                    PreparationIdentityCommand{BeginPinnedPreparation{pin}}};
}

std::optional<Decision> validatePinShape(const PrepareCarvePin& pin) {
    if (!pin.project().valid())
        return noCommand(Status::InvalidIdentity, Issue::InvalidProject);
    if (!pin.modelItem().valid())
        return noCommand(Status::InvalidIdentity, Issue::InvalidModelItem);
    if (!pin.modelSource().valid() || pin.modelSource().kind != workshop::LibraryItemKind::Model) {
        return noCommand(Status::InvalidIdentity, Issue::InvalidModelSource);
    }
    if (!pin.operationItem().valid())
        return noCommand(Status::InvalidIdentity, Issue::InvalidOperationItem);
    if (!pin.token().valid())
        return noCommand(Status::InvalidIdentity, Issue::InvalidToken);
    if (pin.modelItem().project != pin.project())
        return noCommand(Status::InvalidIdentity, Issue::CrossProjectModel);
    if (pin.operationItem().project != pin.project())
        return noCommand(Status::InvalidIdentity, Issue::CrossProjectOperation);
    if (pin.modelItem().item == pin.operationItem().item) {
        return noCommand(Status::InvalidIdentity, Issue::ModelAndOperationAreSameItem);
    }
    return std::nullopt;
}

std::optional<Decision> validateSnapshotShape(workshop::ProjectId activeProject,
                                              const std::vector<PreparationItemSnapshot>& items) {
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (!items[index].ref().valid()) {
            return noCommand(Status::InvalidIdentity, Issue::InvalidSnapshotItem);
        }
        if (items[index].ref().project != activeProject) {
            return noCommand(Status::InvalidIdentity, Issue::ForeignSnapshotItem);
        }
        for (std::size_t other = index + 1; other < items.size(); ++other) {
            if (items[index].ref() == items[other].ref()) {
                return noCommand(Status::InvalidIdentity, Issue::DuplicateSnapshotItem);
            }
        }
    }
    return std::nullopt;
}

const PreparationItemSnapshot* findItem(const std::vector<PreparationItemSnapshot>& items,
                                        workshop::ProjectItemRef ref) noexcept {
    for (const auto& item : items) {
        if (item.ref() == ref)
            return &item;
    }
    return nullptr;
}

} // namespace

PrepareCarvePin::PrepareCarvePin(workshop::ProjectId project,
                                 workshop::ProjectItemRef modelItem,
                                 workshop::LibraryItemRef modelSource,
                                 workshop::ProjectItemRef operationItem,
                                 PreparationToken token,
                                 PreparationRevision revision) noexcept
    : m_project(project), m_modelItem(modelItem), m_modelSource(modelSource),
      m_operationItem(operationItem), m_token(token), m_revision(revision) {}

workshop::ProjectId PrepareCarvePin::project() const noexcept {
    return m_project;
}

workshop::ProjectItemRef PrepareCarvePin::modelItem() const noexcept {
    return m_modelItem;
}

workshop::LibraryItemRef PrepareCarvePin::modelSource() const noexcept {
    return m_modelSource;
}

workshop::ProjectItemRef PrepareCarvePin::operationItem() const noexcept {
    return m_operationItem;
}

PreparationToken PrepareCarvePin::token() const noexcept {
    return m_token;
}

PreparationRevision PrepareCarvePin::revision() const noexcept {
    return m_revision;
}

bool operator==(const PrepareCarvePin& lhs, const PrepareCarvePin& rhs) noexcept {
    return lhs.project() == rhs.project() && lhs.modelItem() == rhs.modelItem() &&
           sameLibraryItem(lhs.modelSource(), rhs.modelSource()) &&
           lhs.operationItem() == rhs.operationItem() && lhs.token() == rhs.token() &&
           lhs.revision() == rhs.revision();
}

PreparationItemSnapshot::PreparationItemSnapshot(
    workshop::ProjectItemRef ref,
    PreparationItemKind kind,
    std::optional<workshop::ProjectItemId> parent,
    std::optional<workshop::LibraryItemRef> source) noexcept
    : m_ref(ref), m_kind(kind), m_parent(parent), m_source(source) {}

workshop::ProjectItemRef PreparationItemSnapshot::ref() const noexcept {
    return m_ref;
}

PreparationItemKind PreparationItemSnapshot::kind() const noexcept {
    return m_kind;
}

std::optional<workshop::ProjectItemId> PreparationItemSnapshot::parent() const noexcept {
    return m_parent;
}

std::optional<workshop::LibraryItemRef> PreparationItemSnapshot::source() const noexcept {
    return m_source;
}

PreparationIdentitySnapshot::PreparationIdentitySnapshot(
    std::optional<workshop::ProjectId> activeProject,
    PreparationRevision revision,
    std::vector<PreparationItemSnapshot> items)
    : m_activeProject(activeProject), m_revision(revision), m_items(std::move(items)) {}

std::optional<workshop::ProjectId> PreparationIdentitySnapshot::activeProject() const noexcept {
    return m_activeProject;
}

PreparationRevision PreparationIdentitySnapshot::revision() const noexcept {
    return m_revision;
}

const std::vector<PreparationItemSnapshot>& PreparationIdentitySnapshot::items() const noexcept {
    return m_items;
}

PreparationIdentityDecision PreparationIdentityPolicy::evaluate(
    bool enabled,
    const std::optional<PrepareCarvePin>& proposedPin,
    const PreparationIdentitySnapshot& snapshot) {
    if (!enabled)
        return noCommand(Status::Disabled, Issue::ModuleDisabled);

    if (!snapshot.activeProject().has_value())
        return createProjectRequired();

    if (!snapshot.activeProject()->valid()) {
        return noCommand(Status::InvalidIdentity, Issue::InvalidActiveProject);
    }

    if (!proposedPin.has_value())
        return noCommand(Status::InvalidIdentity, Issue::MissingPin);

    if (const auto invalid = validatePinShape(*proposedPin))
        return *invalid;

    if (*snapshot.activeProject() != proposedPin->project()) {
        return noCommand(Status::StaleIdentity, Issue::ActiveProjectChanged);
    }

    if (snapshot.revision() != proposedPin->revision())
        return noCommand(Status::StaleIdentity, Issue::RevisionChanged);

    if (const auto invalid = validateSnapshotShape(*snapshot.activeProject(), snapshot.items())) {
        return *invalid;
    }

    const auto* model = findItem(snapshot.items(), proposedPin->modelItem());
    if (model == nullptr)
        return noCommand(Status::StaleIdentity, Issue::ModelMissing);
    if (model->kind() != PreparationItemKind::Model)
        return noCommand(Status::InvalidIdentity, Issue::WrongModelKind);
    if (!model->source().has_value() ||
        !sameLibraryItem(*model->source(), proposedPin->modelSource())) {
        return noCommand(Status::StaleIdentity, Issue::ModelSourceChanged);
    }

    const auto* operation = findItem(snapshot.items(), proposedPin->operationItem());
    if (operation == nullptr)
        return noCommand(Status::StaleIdentity, Issue::OperationMissing);
    if (operation->kind() != PreparationItemKind::Operation)
        return noCommand(Status::InvalidIdentity, Issue::WrongOperationKind);
    if (!operation->parent().has_value() || *operation->parent() != proposedPin->modelItem().item) {
        return noCommand(Status::InvalidIdentity, Issue::OperationParentMismatch);
    }

    return ready(*proposedPin);
}

} // namespace dw::carve_preparation

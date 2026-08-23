#pragma once

#include <functional>
#include <optional>

#include "workshop_contract.h"

namespace dw::workshop {

struct ProjectResumeBookmark {
    ProjectId project;
    std::optional<ProjectItemId> item;

    [[nodiscard]] bool valid() const noexcept { return project.valid(); }
};

enum class ProjectResumeLoadStatus {
    Loaded,
    Missing,
    Invalid,
    ReadFailed,
};

struct ProjectResumeLoadResult {
    ProjectResumeLoadStatus status = ProjectResumeLoadStatus::Missing;
    std::optional<ProjectResumeBookmark> bookmark;
};

class ProjectResumeStore {
  public:
    virtual ~ProjectResumeStore() = default;
    [[nodiscard]] virtual ProjectResumeLoadResult load() const = 0;
    [[nodiscard]] virtual bool save(const ProjectResumeBookmark& bookmark) = 0;
    [[nodiscard]] virtual bool clear() = 0;
};

enum class ResumeProjectStatus {
    Ready,
    Missing,
    InvalidStorage,
    IdentityMismatch,
};

enum class ResumeItemStatus {
    Ready,
    Missing,
    ForeignProject,
    Stale,
};

enum class ResumeActivationStatus {
    Applied,
    Pending,
    Rejected,
    Superseded,
};

enum class ProjectClosePurpose {
    ExplicitClose,
    ApplicationExit,
};

enum class ProjectResumeStatus {
    NoBookmark,
    InvalidBookmark,
    PersistenceFailure,
    ActivationRejected,
    ActivationSuperseded,
    ProjectRestored,
    ItemActivationPending,
    ProjectAndItemRestored,
};

struct ProjectResumeResult {
    ProjectResumeStatus status = ProjectResumeStatus::NoBookmark;
    bool projectRestored = false;
    bool itemRestored = false;
    bool persistenceHealthy = true;
};

struct ProjectResumeCallbacks {
    std::function<ResumeProjectStatus(const ProjectResumeBookmark&)> inspectProject;
    std::function<ResumeActivationStatus(ProjectId)> activateProject;
    std::function<ResumeItemStatus(ProjectItemRef)> inspectItem;
    std::function<ResumeActivationStatus(ProjectItemRef)> activateItem;
    std::function<void()> showHome;
};

class ProjectResumeCoordinator final {
  public:
    ProjectResumeCoordinator(ProjectResumeStore& store, ProjectResumeCallbacks callbacks);

    [[nodiscard]] ProjectResumeResult restore();
    [[nodiscard]] bool rememberProject(ProjectId project);
    [[nodiscard]] bool rememberItem(ProjectItemRef item);
    [[nodiscard]] bool clearItem();
    [[nodiscard]] bool completeClose(ProjectClosePurpose purpose);
    [[nodiscard]] bool completeDestruction();

  private:
    void showHome() const;

    ProjectResumeStore& m_store;
    ProjectResumeCallbacks m_callbacks;
};

} // namespace dw::workshop

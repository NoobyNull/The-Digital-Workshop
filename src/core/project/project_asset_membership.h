#pragma once

#include <vector>

#include "../types.h"

namespace dw {

class ProjectManager;

enum class ProjectAssetKind {
    Model,
    GCode,
};

struct ProjectAssetRef {
    ProjectAssetKind kind = ProjectAssetKind::Model;
    i64 sourceId = 0;

    friend bool operator==(ProjectAssetRef lhs, ProjectAssetRef rhs) {
        return lhs.kind == rhs.kind && lhs.sourceId == rhs.sourceId;
    }
    friend bool operator!=(ProjectAssetRef lhs, ProjectAssetRef rhs) { return !(lhs == rhs); }
};

struct ProjectAssetMembershipRequest {
    // The caller pins the project identity it observed before issuing the
    // request. Membership is never redirected to whichever project is current.
    i64 expectedProjectId = 0;
    std::vector<ProjectAssetRef> assets;
};

enum class ProjectAssetMembershipStatus {
    Applied,
    Unchanged,
    Rejected,
};

enum class ProjectAssetMembershipFailure {
    None,
    EmptyRequest,
    InvalidAssetKind,
    InvalidSourceId,
    NoActiveProject,
    ProjectMismatch,
    StorageUnavailable,
    SourceMissing,
    SourceFileMissing,
    ModelLimitExceeded,
    OpenItemMissing,
    TransactionUnavailable,
    AssociationWriteFailed,
    OpenItemWriteFailed,
    ProjectionFailed,
    VerificationFailed,
    ActiveProjectChanged,
    DirectoryPublishFailed,
    TransactionCommitFailed,
    RollbackFailed,
};

enum class ProjectAssetMembershipItemStatus {
    Added,
    AlreadyMember,
    DuplicateRequest,
    InvalidAssetKind,
    InvalidSourceId,
    SourceMissing,
    SourceFileMissing,
    ModelLimitExceeded,
    OpenItemMissing,
    NotCommitted,
};

struct ProjectAssetMembershipItemOutcome {
    ProjectAssetRef asset;
    ProjectAssetMembershipItemStatus status =
        ProjectAssetMembershipItemStatus::NotCommitted;
};

struct ProjectAssetMembershipResult {
    ProjectAssetMembershipStatus status = ProjectAssetMembershipStatus::Rejected;
    ProjectAssetMembershipFailure failure = ProjectAssetMembershipFailure::None;
    std::vector<ProjectAssetMembershipItemOutcome> items;

    [[nodiscard]] bool applied() const noexcept {
        return status == ProjectAssetMembershipStatus::Applied;
    }
};

// UI-free application service for the single operation "ensure these Library
// assets belong to this exact active project". The service owns request
// normalization; ProjectManager owns the one durable transaction and storage
// publication gateway.
class ProjectAssetMembershipService final {
  public:
    explicit ProjectAssetMembershipService(ProjectManager& projects) : m_projects(projects) {}

    [[nodiscard]] ProjectAssetMembershipResult
    ensure(const ProjectAssetMembershipRequest& request);

  private:
    ProjectManager& m_projects;
};

} // namespace dw

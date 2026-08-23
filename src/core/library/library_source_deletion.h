#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../types.h"

namespace dw {

class GCodeRepository;
class LibraryManager;
class ProjectRepository;

enum class LibrarySourceKind {
    Model,
    GCode,
};

// A Library identity that is independent of UI selection and picker types.
struct LibrarySourceRef {
    LibrarySourceKind kind = LibrarySourceKind::Model;
    i64 id = 0;
};

bool operator==(const LibrarySourceRef& lhs, const LibrarySourceRef& rhs);

struct LibrarySourceProjectRef {
    i64 id = 0;
    std::string name;
};

enum class LibrarySourceDeletionItemStatus {
    BatchBlocked,
    InvalidSource,
    MissingSource,
    ActivePreview,
    LinkedToProjects,
    Deleted,
    DeleteFailed,
};

struct LibrarySourceDeletionItemResult {
    LibrarySourceRef source;
    LibrarySourceDeletionItemStatus status = LibrarySourceDeletionItemStatus::BatchBlocked;
    std::vector<LibrarySourceProjectRef> affectedProjects;
    bool duplicate = false;

    // UI selection may only be cleared when the underlying source is confirmed gone.
    [[nodiscard]] bool selectionCanClear() const {
        return status == LibrarySourceDeletionItemStatus::Deleted;
    }
};

enum class LibrarySourceDeletionStatus {
    EmptyRequest,
    PreflightRejected,
    Deleted,
    PartiallyDeleted,
    DeletionFailed,
};

struct LibrarySourceDeletionResult {
    LibrarySourceDeletionStatus status = LibrarySourceDeletionStatus::EmptyRequest;
    std::vector<LibrarySourceDeletionItemResult> items;
    std::vector<LibrarySourceProjectRef> affectedProjects;
};

// Owns the safety policy for destructive Library source deletion. The complete
// batch is preflighted before any mutation is attempted.
class LibrarySourceDeletionService {
  public:
    LibrarySourceDeletionService(LibraryManager& library,
                                 ProjectRepository& projects,
                                 GCodeRepository& gcodes);

    [[nodiscard]] LibrarySourceDeletionResult deleteSources(
        const std::vector<LibrarySourceRef>& sources,
        std::optional<LibrarySourceRef> activePreview = std::nullopt);

  private:
    LibraryManager& m_library;
    ProjectRepository& m_projects;
    GCodeRepository& m_gcodes;
};

} // namespace dw

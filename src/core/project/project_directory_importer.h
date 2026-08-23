#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "../types.h"

namespace dw {

class Database;
class LibraryManager;
class ProjectDirectory;

enum class ProjectDirectoryImportError {
    None,
    InvalidRoot,
    ManifestMissing,
    InvalidManifest,
    UnsupportedManifestVersion,
    UnsafeEntry,
    MissingEntry,
    HashMismatch,
    InvalidGCode,
    ModelImportFailed,
    GCodeStorageFailed,
    GCodeImportFailed,
    ProjectCreateFailed,
    ProjectAssociationFailed,
    ProjectVerificationFailed,
    TransactionBeginFailed,
    TransactionCommitFailed,
    ProjectRollbackFailed,
};

struct ProjectDirectoryImportResult {
    std::optional<i64> projectId;
    Path canonicalRoot;
    ProjectDirectoryImportError error = ProjectDirectoryImportError::None;
    std::string message;
    std::size_t modelCount = 0;
    std::size_t gcodeCount = 0;

    [[nodiscard]] bool success() const noexcept {
        return projectId.has_value() && error == ProjectDirectoryImportError::None;
    }
};

// Hydrates an already-opened project folder into database/library records.
// Construction is the enable seam: when this service is not composed and
// invoked, it performs no filesystem or database mutations.
class ProjectDirectoryImporter {
  public:
    ProjectDirectoryImporter(Database& database, LibraryManager& library);

    [[nodiscard]] ProjectDirectoryImportResult hydrate(const ProjectDirectory& directory);

  private:
    Database& m_database;
    LibraryManager& m_library;
};

} // namespace dw

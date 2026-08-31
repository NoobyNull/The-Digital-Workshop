#pragma once

#include <memory>
#include <string>

#include "../types.h"

namespace dw {

class Project;
class ProjectManager;

struct NamedProjectCreationToken {
    i64 projectId = 0;
    Path root;

    [[nodiscard]] bool valid() const noexcept { return projectId > 0 && !root.empty(); }
};

enum class NamedProjectPrepareStatus {
    Prepared,
    CreateFailed,
    StorageFailed,
    CleanupFailed,
};

struct NamedProjectPrepareResult {
    NamedProjectPrepareStatus status = NamedProjectPrepareStatus::CreateFailed;
    NamedProjectCreationToken token;
    std::shared_ptr<Project> project;

    [[nodiscard]] bool prepared() const noexcept {
        return status == NamedProjectPrepareStatus::Prepared && token.valid() && project != nullptr;
    }
};

enum class NamedProjectFinishStatus {
    Published,
    NeedsSaving,
    RejectedCleaned,
    CleanupFailed,
    ActiveIdentityMismatch,
    InvalidToken,
};

class NamedProjectCreationService final {
  public:
    explicit NamedProjectCreationService(ProjectManager& projectManager);

    [[nodiscard]] NamedProjectPrepareResult prepare(std::string name);
    [[nodiscard]] NamedProjectFinishStatus finish(const NamedProjectCreationToken& token,
                                                  bool activationAccepted);

  private:
    ProjectManager& m_projectManager;
};

} // namespace dw

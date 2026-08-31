#pragma once

#include <optional>
#include <vector>

#include "core/database/project_repository.h"
#include "modules/carve_preparation/preparation_identity.h"

namespace dw {

enum class PrepareCarveAdapterStatus {
    Ready,
    CreateProjectRequired,
    OperationRequired,
    TargetUnavailable,
    AmbiguousOperation,
    InvalidHierarchy,
    InvalidModelSource,
    PolicyRejected,
};

struct PrepareCarveAdapterResult {
    PrepareCarveAdapterStatus status = PrepareCarveAdapterStatus::TargetUnavailable;
    std::optional<carve_preparation::PrepareCarvePin> pin;
    carve_preparation::PreparationIdentityIssue issue =
        carve_preparation::PreparationIdentityIssue::None;
};

[[nodiscard]] carve_preparation::PreparationIdentitySnapshot
makePreparationIdentitySnapshot(
    std::optional<workshop::ProjectId> activeProject,
    carve_preparation::PreparationRevision revision,
    const std::vector<ProjectOpenItem>& items);

[[nodiscard]] PrepareCarveAdapterResult resolvePrepareCarvePin(
    std::optional<workshop::ProjectId> activeProject,
    workshop::ProjectItemRef target,
    carve_preparation::PreparationToken token,
    carve_preparation::PreparationRevision revision,
    const std::vector<ProjectOpenItem>& items);

} // namespace dw

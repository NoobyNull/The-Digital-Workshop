#pragma once

#include <optional>
#include <string>

#include "core/types.h"
#include "modules/workshop/workshop_contract.h"

namespace dw {

class LibraryManager;
class ProjectManager;
class ProjectSessionIntegration;

struct UxCaptureFixture {
    workshop::ProjectId project;
    workshop::LibraryItemRef primary;
    workshop::LibraryItemRef alternate;
    workshop::LibraryItemRef previewOnly;
    workshop::ProjectItemRef primaryProjectItem;
    Path projectRoot;
    std::string projectName = "River Sign";
    std::string primaryName = "Primary";
    std::string alternateName = "Alternate";
    std::string previewName = "Preview Only";

    [[nodiscard]] bool valid() const noexcept {
        return project.valid() && primary.valid() && alternate.valid() &&
               previewOnly.valid() && primaryProjectItem.valid() &&
               !projectRoot.empty();
    }
};

[[nodiscard]] std::optional<UxCaptureFixture>
seedUxCaptureFixture(LibraryManager& library,
                     ProjectManager& projects,
                     ProjectSessionIntegration& sessionIntegration,
                     std::string& error);

} // namespace dw

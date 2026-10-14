#pragma once

#include <vector>

#include "core/database/project_repository.h"
#include "ui/panels/project_contents.h"

namespace dw {

// Projects stored project items into the rows the Project panel shows. Items
// from another project are dropped, and parents always precede children.
[[nodiscard]] ProjectContents makeProjectContents(workshop::ProjectId project,
                                                  const std::vector<ProjectOpenItem>& items);

} // namespace dw

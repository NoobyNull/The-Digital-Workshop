#pragma once

#include <string>
#include <vector>

#include "../database/project_repository.h"

namespace dw {

std::vector<std::string>
buildProjectOpenItemWarningLines(const std::vector<ProjectOpenItem>& items);

} // namespace dw

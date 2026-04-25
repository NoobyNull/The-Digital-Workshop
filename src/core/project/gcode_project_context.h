#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "../database/project_repository.h"
#include "../types.h"

namespace dw {

std::vector<std::string>
buildGCodeProjectContextLines(std::string_view projectName,
                              const std::vector<ProjectOpenItem>& openItems,
                              i64 gcodeId);

} // namespace dw

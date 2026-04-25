#pragma once

#include <optional>
#include <string>

#include "model_fitter.h"
#include "toolpath_types.h"
#include "../cnc/cnc_tool.h"
#include "../database/project_repository.h"

namespace dw {
namespace carve {

struct DirectCarveOperationSetup {
    std::string modelName;
    Path modelSourcePath;
    std::optional<i64> materialId;
    std::string materialName;
    StockDimensions stock;
    FitParams fit;
    ToolpathConfig toolpath;
    std::optional<VtdbToolGeometry> finishingTool;
    std::optional<VtdbToolGeometry> clearingTool;
};

std::optional<DirectCarveOperationSetup>
parseDirectCarveOperationSetup(const ProjectOpenItem& item);

} // namespace carve
} // namespace dw

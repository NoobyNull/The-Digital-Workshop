#pragma once

#include "toolpath_types.h"
#include "../cnc/cnc_tool.h"
#include "../types.h"

#include <optional>
#include <string>
#include <vector>

namespace dw {
namespace carve {

struct RoughingToolSelection {
    std::optional<VtdbToolGeometry> tool;
    bool requiresToolChange = false;
    std::string warning;
};

RoughingToolSelection selectFixedDepthRoughingTool(
    const std::vector<VtdbToolGeometry>& toolboxTools,
    const VtdbToolGeometry& finishTool,
    const Vec3& stockMin,
    const Vec3& stockMax,
    const Vec3& modelMin,
    const Vec3& modelMax,
    CutExtents cutExtents = CutExtents::Model);

} // namespace carve
} // namespace dw

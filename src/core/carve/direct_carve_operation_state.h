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

enum class DirectCarveTouchPlate {
    Generic,
    SienciAutoZero,
};

enum class DirectCarveZeroProbeMode {
    ZOnly,
    XOnly,
    YOnly,
    XYCorner,
    XYZAuto,
};

enum class DirectCarveAutoZeroBitMode {
    Auto,
    Tip,
};

enum class DirectCarveZeroCorner {
    FrontLeft,
    FrontRight,
    BackRight,
    BackLeft,
};

struct DirectCarveZeroingSetup {
    DirectCarveTouchPlate touchPlate = DirectCarveTouchPlate::Generic;
    DirectCarveZeroProbeMode probeMode = DirectCarveZeroProbeMode::ZOnly;
    DirectCarveAutoZeroBitMode bitMode = DirectCarveAutoZeroBitMode::Auto;
    DirectCarveZeroCorner corner = DirectCarveZeroCorner::FrontLeft;
    f32 zPlateThicknessMm = 15.0f;
    f32 xyWallThicknessMm = 10.0f;
    f32 fastProbeMmMin = 150.0f;
    f32 slowProbeMmMin = 75.0f;
    f32 searchDistanceMm = 30.0f;
    f32 retractMm = 2.0f;
    f32 autoZeroOriginOffsetMm = 22.5f;
    f32 autoZeroFinalZRetractMm = 1.0f;
    f32 toolDiameterMm = 0.0f;
    bool zeroVerified = false;
};

std::optional<DirectCarveOperationSetup>
parseDirectCarveOperationSetup(const ProjectOpenItem& item);

ProjectOpenItem makeDirectCarveZeroingOpenItem(
    i64 operationItemId,
    const std::string& operationSourceKey,
    const DirectCarveZeroingSetup& setup);

std::optional<DirectCarveZeroingSetup>
parseDirectCarveZeroingSetup(const ProjectOpenItem& item);

} // namespace carve
} // namespace dw

#include "roughing_tool_selector.h"

#include <algorithm>
#include <cmath>

namespace dw {
namespace carve {
namespace {

constexpr f64 kMinRoughingDiameterMm = 3.0;

f64 toolDiameterMm(const VtdbToolGeometry& tool)
{
    return tool.units == VtdbUnits::Imperial
        ? tool.diameter * 25.4
        : tool.diameter;
}

bool isFlatRoughingTool(const VtdbToolGeometry& tool)
{
    return tool.tool_type == VtdbToolType::EndMill ||
           tool.tool_type == VtdbToolType::Radiused;
}

bool sameTool(const VtdbToolGeometry& a, const VtdbToolGeometry& b)
{
    if (!a.id.empty() && !b.id.empty()) {
        return a.id == b.id;
    }

    return a.tool_type == b.tool_type &&
           a.units == b.units &&
           std::abs(a.diameter - b.diameter) < 0.0001 &&
           std::abs(a.flat_diameter - b.flat_diameter) < 0.0001 &&
           std::abs(a.tip_radius - b.tip_radius) < 0.0001;
}

bool canClearOutsideModel(const Vec3& stockMin,
                          const Vec3& stockMax,
                          const Vec3& modelMin,
                          const Vec3& modelMax,
                          f64 toolDiameter)
{
    const f32 sx0 = std::min(stockMin.x, stockMax.x);
    const f32 sx1 = std::max(stockMin.x, stockMax.x);
    const f32 sy0 = std::min(stockMin.y, stockMax.y);
    const f32 sy1 = std::max(stockMin.y, stockMax.y);
    if (sx1 <= sx0 || sy1 <= sy0 || toolDiameter <= 0.0) return false;

    const f32 mx0 = std::min(modelMin.x, modelMax.x);
    const f32 mx1 = std::max(modelMin.x, modelMax.x);
    const f32 my0 = std::min(modelMin.y, modelMax.y);
    const f32 my1 = std::max(modelMin.y, modelMax.y);
    if (mx1 <= mx0 || my1 <= my0) return false;

    const f32 radius = static_cast<f32>(toolDiameter * 0.5);
    const f32 noCutMinX = std::clamp(mx0 - radius, sx0, sx1);
    const f32 noCutMaxX = std::clamp(mx1 + radius, sx0, sx1);
    const f32 noCutMinY = std::clamp(my0 - radius, sy0, sy1);
    const f32 noCutMaxY = std::clamp(my1 + radius, sy0, sy1);

    constexpr f32 kEpsilon = 0.001f;
    return noCutMinX > sx0 + kEpsilon ||
           noCutMaxX < sx1 - kEpsilon ||
           noCutMinY > sy0 + kEpsilon ||
           noCutMaxY < sy1 - kEpsilon;
}

} // namespace

RoughingToolSelection selectFixedDepthRoughingTool(
    const std::vector<VtdbToolGeometry>& toolboxTools,
    const VtdbToolGeometry& finishTool,
    const Vec3& stockMin,
    const Vec3& stockMax,
    const Vec3& modelMin,
    const Vec3& modelMax,
    CutExtents cutExtents)
{
    RoughingToolSelection result;

    std::vector<VtdbToolGeometry> candidates;
    if (isFlatRoughingTool(finishTool)) {
        candidates.push_back(finishTool);
    }
    for (const auto& tool : toolboxTools) {
        if (!isFlatRoughingTool(tool)) continue;

        bool alreadyAdded = false;
        for (const auto& candidate : candidates) {
            if (sameTool(candidate, tool)) {
                alreadyAdded = true;
                break;
            }
        }
        if (!alreadyAdded) {
            candidates.push_back(tool);
        }
    }

    std::optional<VtdbToolGeometry> best;
    f64 bestDiameter = 0.0;
    for (const auto& tool : candidates) {
        const f64 diameter = toolDiameterMm(tool);
        if (diameter < kMinRoughingDiameterMm) continue;
        if (cutExtents == CutExtents::Model &&
            !canClearOutsideModel(stockMin, stockMax, modelMin, modelMax,
                                  diameter)) {
            continue;
        }
        if (!best || diameter > bestDiameter) {
            best = tool;
            bestDiameter = diameter;
        }
    }

    if (!best) {
        result.warning =
            "No suitable roughing tool found; skipping auto clear.";
        return result;
    }

    result.tool = best;
    result.requiresToolChange = !sameTool(*best, finishTool);
    return result;
}

} // namespace carve
} // namespace dw

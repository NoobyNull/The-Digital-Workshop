#include "direct_carve_tool_plan.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace dw::carve {
namespace {

void appendGeometryNumber(std::ostringstream& stream, f64 value) {
    stream << ':' << value;
}

bool equalsAsciiIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        char value = lhs[index];
        if (value >= 'A' && value <= 'Z') {
            value = static_cast<char>(value - 'A' + 'a');
        }
        if (value != rhs[index])
            return false;
    }
    return true;
}

} // namespace

std::string stableDirectCarveToolIdentity(const VtdbToolGeometry& tool) {
    if (!tool.id.empty())
        return "uuid:" + tool.id;

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<f64>::max_digits10)
           << "geometry:" << static_cast<int>(tool.tool_type) << ':'
           << static_cast<int>(tool.units);
    appendGeometryNumber(stream, tool.diameter);
    appendGeometryNumber(stream, tool.included_angle);
    appendGeometryNumber(stream, tool.flat_diameter);
    stream << ':' << tool.num_flutes;
    appendGeometryNumber(stream, tool.flute_length);
    appendGeometryNumber(stream, tool.thread_pitch);
    appendGeometryNumber(stream, tool.tip_radius);
    stream << ':' << tool.laser_watt;
    appendGeometryNumber(stream, tool.tooth_size);
    appendGeometryNumber(stream, tool.tooth_offset);
    appendGeometryNumber(stream, tool.neck_length);
    appendGeometryNumber(stream, tool.tooth_height);
    appendGeometryNumber(stream, tool.threaded_length);
    return stream.str();
}

bool sameDirectCarveToolIdentity(const VtdbToolGeometry& lhs, const VtdbToolGeometry& rhs) {
    return stableDirectCarveToolIdentity(lhs) == stableDirectCarveToolIdentity(rhs);
}

bool isSupportedClearingTool(const VtdbToolGeometry& tool) {
    return tool.tool_type == VtdbToolType::EndMill || tool.tool_type == VtdbToolType::Radiused;
}

bool isUsableDirectCarveTool(const VtdbToolGeometry& tool) {
    const f64 diameterMm = tool.units == VtdbUnits::Imperial ? tool.diameter * 25.4 : tool.diameter;
    return std::isfinite(diameterMm) && diameterMm > 0.0 && tool.num_flutes > 0;
}

VtdbToolGeometry makeDirectCarveManualTool(
    VtdbToolType type, f64 diameterMm, int fluteCount, f64 includedAngleDeg, f64 tipRadiusMm) {
    VtdbToolGeometry tool;
    tool.tool_type = type;
    tool.units = VtdbUnits::Metric;
    tool.diameter = diameterMm;
    tool.num_flutes = fluteCount;
    if (type == VtdbToolType::VBit) {
        tool.included_angle = includedAngleDeg;
    } else if (type == VtdbToolType::BallNose || type == VtdbToolType::TaperedBallNose ||
               type == VtdbToolType::Radiused) {
        tool.tip_radius = tipRadiusMm;
    }
    return tool;
}

std::string_view clearingToolModeKey(ClearingToolMode mode) noexcept {
    switch (mode) {
    case ClearingToolMode::Selected:
        return "selected";
    case ClearingToolMode::Disabled:
        return "disabled";
    case ClearingToolMode::Automatic:
    default:
        return "automatic";
    }
}

ClearingToolMode parseClearingToolModeKey(std::string_view key) noexcept {
    if (equalsAsciiIgnoreCase(key, "selected"))
        return ClearingToolMode::Selected;
    if (equalsAsciiIgnoreCase(key, "disabled"))
        return ClearingToolMode::Disabled;
    return ClearingToolMode::Automatic;
}

const std::optional<VtdbToolGeometry>& DirectCarveToolPlan::finishingIntent() const noexcept {
    return m_finishingIntent;
}

const std::optional<VtdbToolGeometry>& DirectCarveToolPlan::clearingIntent() const noexcept {
    return m_clearingIntent;
}

const std::optional<VtdbToolGeometry>& DirectCarveToolPlan::effectiveClearingTool() const noexcept {
    return m_effectiveClearingTool;
}

ClearingToolMode DirectCarveToolPlan::clearingMode() const noexcept {
    return m_clearingMode;
}

bool DirectCarveToolPlan::selectionComplete() const noexcept {
    return m_finishingIntent.has_value() &&
           (m_clearingMode != ClearingToolMode::Selected || m_clearingIntent.has_value());
}

DirectCarveToolSelectionResult DirectCarveToolPlan::selectTool(DirectCarveToolPickerRole role,
                                                               const VtdbToolGeometry& tool) {
    if (!isUsableDirectCarveTool(tool)) {
        return {DirectCarveToolSelectionError::IncompleteToolGeometry,
                "The tool needs a positive diameter and flute count."};
    }
    if (role == DirectCarveToolPickerRole::Clearing && !isSupportedClearingTool(tool)) {
        return {DirectCarveToolSelectionError::UnsupportedClearingTool,
                "Clearing requires a flat End Mill or Radiused tool."};
    }

    if (role == DirectCarveToolPickerRole::Finishing) {
        m_finishingIntent = tool;
    } else {
        m_clearingIntent = tool;
        m_clearingMode = ClearingToolMode::Selected;
    }
    invalidateEffectiveClearing();
    return {};
}

void DirectCarveToolPlan::clearTool(DirectCarveToolPickerRole role) {
    if (role == DirectCarveToolPickerRole::Finishing) {
        m_finishingIntent.reset();
    } else {
        m_clearingIntent.reset();
    }
    invalidateEffectiveClearing();
}

void DirectCarveToolPlan::setClearingMode(ClearingToolMode mode) {
    if (m_clearingMode == mode)
        return;
    m_clearingMode = mode;
    invalidateEffectiveClearing();
}

std::optional<VtdbToolGeometry> DirectCarveToolPlan::resolveClearingIntent(
    const std::optional<VtdbToolGeometry>& automaticRecommendation) const {
    switch (m_clearingMode) {
    case ClearingToolMode::Automatic:
        if (automaticRecommendation && isSupportedClearingTool(*automaticRecommendation) &&
            isUsableDirectCarveTool(*automaticRecommendation)) {
            return automaticRecommendation;
        }
        return std::nullopt;
    case ClearingToolMode::Selected:
        return m_clearingIntent;
    case ClearingToolMode::Disabled:
        return std::nullopt;
    }
    return std::nullopt;
}

bool DirectCarveToolPlan::confirmEffectiveClearing(const std::optional<VtdbToolGeometry>& usedTool,
                                                   bool generatedNonemptyClearingPass) {
    if (!generatedNonemptyClearingPass || !usedTool || !isSupportedClearingTool(*usedTool) ||
        !isUsableDirectCarveTool(*usedTool)) {
        m_effectiveClearingTool.reset();
        return false;
    }

    if (m_clearingMode == ClearingToolMode::Disabled ||
        (m_clearingMode == ClearingToolMode::Selected &&
         (!m_clearingIntent || !sameDirectCarveToolIdentity(*m_clearingIntent, *usedTool)))) {
        m_effectiveClearingTool.reset();
        return false;
    }

    m_effectiveClearingTool = *usedTool;
    return true;
}

void DirectCarveToolPlan::invalidateEffectiveClearing() noexcept {
    m_effectiveClearingTool.reset();
}

} // namespace dw::carve

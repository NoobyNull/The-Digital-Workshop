#include "direct_carve_probe_tool_diameter.h"

#include <algorithm>
#include <cmath>

#include "direct_carve_tool_plan.h"

namespace dw::carve {
namespace {

f32 diameterMm(const VtdbToolGeometry& tool) {
    const f64 diameter = tool.units == VtdbUnits::Imperial ? tool.diameter * 25.4 : tool.diameter;
    return std::isfinite(diameter) ? std::max(0.0F, static_cast<f32>(diameter)) : 0.0F;
}

} // namespace

f32 DirectCarveProbeToolDiameter::valueMm() const noexcept {
    return m_valueMm;
}

bool DirectCarveProbeToolDiameter::manualOverride() const noexcept {
    return m_manualOverride;
}

bool DirectCarveProbeToolDiameter::refreshAutomatic(const VtdbToolGeometry* initialTool) {
    if (m_manualOverride)
        return false;

    const std::string identity = initialTool ? stableDirectCarveToolIdentity(*initialTool)
                                             : std::string{};
    const f32 expectedValue = initialTool ? diameterMm(*initialTool) : 0.0F;
    if (identity == m_automaticSourceIdentity && std::abs(expectedValue - m_valueMm) <= 0.0001F) {
        return false;
    }

    const f32 previous = m_valueMm;
    m_automaticSourceIdentity = identity;
    m_valueMm = expectedValue;
    return std::abs(previous - m_valueMm) > 0.0001F;
}

void DirectCarveProbeToolDiameter::setManualValue(f32 diameterMmValue) noexcept {
    m_valueMm = std::isfinite(diameterMmValue) ? std::max(0.0F, diameterMmValue) : 0.0F;
    m_manualOverride = true;
}

bool DirectCarveProbeToolDiameter::resumeAutomatic(const VtdbToolGeometry* initialTool) {
    const f32 previous = m_valueMm;
    m_manualOverride = false;
    m_automaticSourceIdentity.clear();
    (void)refreshAutomatic(initialTool);
    return std::abs(previous - m_valueMm) > 0.0001F;
}

void DirectCarveProbeToolDiameter::reset() noexcept {
    m_valueMm = 0.0F;
    m_manualOverride = false;
    m_automaticSourceIdentity.clear();
}

} // namespace dw::carve

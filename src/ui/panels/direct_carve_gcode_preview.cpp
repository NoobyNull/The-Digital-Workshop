// Exact generated-program preparation and presentation for Direct Carve.

#include "ui/panels/direct_carve_panel.h"

#include <limits>
#include <sstream>
#include <utility>

#include "core/carve/carve_job.h"
#include "core/carve/gcode_export.h"
#include "core/config/config.h"
#include "core/gcode/gcode_document.h"
#include "ui/tool_library_access.h"

namespace dw {

namespace {

std::string analysisProfileKey(const gcode::MachineProfile& profile) {
    std::ostringstream key;
    key.precision(std::numeric_limits<f32>::max_digits10);
    key << profile.maxFeedRateX << ':'
        << profile.maxFeedRateY << ':'
        << profile.maxFeedRateZ << ':'
        << profile.accelX << ':'
        << profile.accelY << ':'
        << profile.accelZ << ':'
        << profile.rapidRate << ':'
        << profile.defaultFeedRate;
    return key.str();
}

} // namespace

const gcode::PreparedDocument*
DirectCarvePanel::ensureCurrentGCodeDocument() {
    if (!hasCurrentToolpath()) return nullptr;
    const auto& finishingTool = m_toolPlan.finishingIntent();
    if (!finishingTool) return nullptr;

    const auto units = detectedSendUnits();
    const auto& profile = Config::instance().getActiveMachineProfile();
    const auto profileKey = analysisProfileKey(profile);
    if (m_preparedGCode &&
        m_preparedGCodeVersion == m_generatedAtVersion &&
        m_preparedGCodeUnits == units &&
        m_preparedGCodeProfileKey == profileKey) {
        return m_preparedGCode.get();
    }

    auto exactText = carve::generateGcode(
        m_carveJob->toolpath(),
        m_toolpathConfig,
        m_modelName,
        resolveToolNameFormat(*finishingTool),
        units);
    auto prepared = gcode::prepareDocument(std::move(exactText), profile);
    if (!prepared.hasCommands() || !prepared.hasMotion()) {
        m_preparedGCode.reset();
        m_preparedGCodeVersion = -1;
        m_preparedGCodeUnits.reset();
        m_preparedGCodeProfileKey.clear();
        return nullptr;
    }

    m_preparedGCode =
        std::make_shared<const gcode::PreparedDocument>(std::move(prepared));
    m_preparedGCodeVersion = m_generatedAtVersion;
    m_preparedGCodeUnits = units;
    m_preparedGCodeProfileKey = std::move(profileKey);
    return m_preparedGCode.get();
}

void DirectCarvePanel::publishGCode3DPreview() {
    if (!m_onGCode3DPreview) return;
    const auto* prepared = ensureCurrentGCodeDocument();
    if (!prepared) return;

    m_onGCode3DPreview(*prepared);
    m_gcode3DPreviewPublished = true;
}

void DirectCarvePanel::clearGCode3DPreview() {
    m_preparedGCode.reset();
    m_preparedGCodeVersion = -1;
    m_preparedGCodeUnits.reset();
    m_preparedGCodeProfileKey.clear();

    if (m_gcode3DPreviewPublished && m_onGCode3DPreviewCleared) {
        m_onGCode3DPreviewCleared();
    }
    m_gcode3DPreviewPublished = false;
}

} // namespace dw

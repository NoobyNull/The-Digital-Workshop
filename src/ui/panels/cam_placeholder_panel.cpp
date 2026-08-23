#include "cam_placeholder_panel.h"

#include <imgui.h>

namespace dw {

const std::string& CamPlaceholderPanel::statusCopy() const noexcept {
    static const std::string copy =
        "The CAM workspace is being rebuilt on the PureCutCNC engine.\n"
        "Carve preparation and toolpath generation return when the "
        "rebuild ships.\nExternal G-code workflows are unaffected.";
    return copy;
}

void CamPlaceholderPanel::setEngineStatusProvider(std::function<std::string()> provider) {
    m_engineStatusProvider = std::move(provider);
}

std::string CamPlaceholderPanel::engineStatusLine() const {
    return m_engineStatusProvider ? m_engineStatusProvider() : "Engine not started";
}

void CamPlaceholderPanel::setOnStartEngine(std::function<void()> onStartEngine) {
    m_onStartEngine = std::move(onStartEngine);
}

void CamPlaceholderPanel::render() {
    if (!m_open)
        return;

    if (!ImGui::Begin(m_title.c_str(), &m_open)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("%s", statusCopy().c_str());
    ImGui::Separator();
    ImGui::TextWrapped("%s", engineStatusLine().c_str());
    // Phase 2 manual verification surface; Phase 3 replaces this button
    // with automatic/managed engine startup.
    if (ImGui::Button("Start engine") && m_onStartEngine) {
        m_onStartEngine();
    }
    ImGui::End();
}

} // namespace dw

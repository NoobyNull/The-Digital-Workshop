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

void CamPlaceholderPanel::render() {
    if (!m_open)
        return;

    if (!ImGui::Begin(m_title.c_str(), &m_open)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("%s", statusCopy().c_str());
    ImGui::End();
}

} // namespace dw

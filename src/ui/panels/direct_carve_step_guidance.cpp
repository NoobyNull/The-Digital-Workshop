// Snapshot-driven beginner guidance shared by the four preparation views.

#include "ui/panels/direct_carve_panel.h"

#include <imgui.h>

namespace dw {

bool DirectCarvePanel::renderPreparationStepGuidance(
    carve_preparation::PreparationStageId stage) {
    const auto& snapshot = m_preparationFlow.snapshot();
    const auto* pin = snapshot.pin();
    if (!snapshot.active || !pin || !m_preparationPin ||
        !(*pin == *m_preparationPin)) {
        return false;
    }

    const carve_preparation::PreparationStepFacts facts(
        *pin, snapshot.stage(stage));
    const auto presentation =
        carve_preparation::buildPreparationStepPresentation(facts);

    ImGui::TextUnformatted(presentation.title.c_str());
    ImGui::TextWrapped("%s", presentation.whyItMatters.c_str());
    if (!presentation.nextGuidance.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Next: %s", presentation.nextGuidance.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    return true;
}

} // namespace dw

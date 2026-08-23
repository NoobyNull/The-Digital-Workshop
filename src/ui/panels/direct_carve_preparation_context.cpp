// Preparation identity banner and dirty-state notification for Direct Carve.

#include "ui/panels/direct_carve_panel.h"

#include <imgui.h>

#include "ui/ui_colors.h"

namespace dw {

void DirectCarvePanel::clearFinalConfirmation() {
    m_commitConfirmed = false;
    m_commitConfirmedSettingsVersion = -1;
    m_commitConfirmedToolpathVersion = -1;
}

void DirectCarvePanel::markToolpathSettingsChanged() {
    clearGCode3DPreview();
    ++m_settingsVersion;
    m_savedRunToolpath.reset();
    m_toolPlan.invalidateEffectiveClearing();
    m_autoRoughingWarning.clear();
    m_surfaceToolpathPending = false;
    m_surfaceToolpathPendingVersion = -1;
    m_zeroConfirmed = false;
    m_outlineCompleted = false;
    m_outlineSkipped = false;
    m_outlineRunning = false;
    m_outlineCmdIndex = 0;
    clearFinalConfirmation();
    if (!m_restoringOperationSetup)
        setPreparationDirty(true);
}

void DirectCarvePanel::markToolPlanChanged() {
    markToolpathSettingsChanged();
}

void DirectCarvePanel::markGeometryChanged() {
    markToolpathSettingsChanged();
    m_toolpathGenerated = false;
    m_generatedAtVersion = -1;
}

void DirectCarvePanel::setPreparationDirty(bool dirty) {
    if (dirty && !m_preparationPin)
        return;
    if (m_preparationDirty == dirty)
        return;

    m_preparationDirty = dirty;
    if (m_onPreparationDirty)
        m_onPreparationDirty(dirty);
}

void DirectCarvePanel::renderPreparationContext() {
    if (!m_modelLoaded)
        return;
    if (m_preparationPin) {
        if (m_preparationDirty) {
            ImGui::TextColored(colors::kWarning, "Project preparation has unsaved changes.");
            ImGui::Spacing();
        }
        return;
    }

    ImGui::TextWrapped(
        "Standalone preparation: export remains available, but nothing will be added to a "
        "project until you explicitly create one.");
    if (!m_onCreateProjectRequested)
        ImGui::BeginDisabled();
    if (ImGui::Button("Create Project") && m_onCreateProjectRequested)
        m_onCreateProjectRequested();
    if (!m_onCreateProjectRequested)
        ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

} // namespace dw

// Direct Carve: Material & Blank planning step.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>

#include <imgui.h>

#include "core/config/config.h"
#include "core/gcode/machine_profile.h"
#include "core/gcode/machine_rigidity.h"
#include "core/materials/material_manager.h"
#include "ui/ui_colors.h"

namespace dw {

namespace {
constexpr auto& kYellow = colors::kWarning;
} // namespace

void DirectCarvePanel::renderMaterialSetup() {
    if (!renderPreparationStepGuidance(
            carve_preparation::PreparationStageId::MaterialAndBlank)) {
        ImGui::TextUnformatted("Material & Blank");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Choose what the blank is made from before choosing a tool. Material hardness "
            "changes sensible feed and stepdown suggestions, so this gives the next step "
            "the context it needs.");
    }
    ImGui::TextDisabled("Blank: %.1f x %.1f x %.1f mm",
                        static_cast<double>(m_stock.width),
                        static_cast<double>(m_stock.height),
                        static_cast<double>(m_stock.thickness));
    ImGui::Spacing();

    // Load materials from database on first visit
    if (!m_materialListLoaded && m_materialMgr) {
        m_materialListLoaded = true;
        m_materialList = m_materialMgr->getAllMaterials();
    }

    float iw = ImGui::GetContentRegionAvail().x * 0.45f;

    // --- Material selector ---
    if (!m_materialList.empty()) {
        ImGui::SetNextItemWidth(iw);
        const char* preview =
            (m_selectedMaterialIdx >= 0)
                ? m_materialList[static_cast<size_t>(m_selectedMaterialIdx)].name.c_str()
                : "Select material...";
        if (ImGui::BeginCombo("Material", preview)) {
            // Group by category
            const char* catLabels[] = {"Hardwood", "Softwood", "Domestic", "Composite"};
            MaterialCategory cats[] = {MaterialCategory::Hardwood,
                                       MaterialCategory::Softwood,
                                       MaterialCategory::Domestic,
                                       MaterialCategory::Composite};
            for (int c = 0; c < 4; ++c) {
                bool hasItems = false;
                for (int i = 0; i < static_cast<int>(m_materialList.size()); ++i) {
                    if (m_materialList[static_cast<size_t>(i)].category != cats[c])
                        continue;
                    if (!hasItems) {
                        ImGui::SeparatorText(catLabels[c]);
                        hasItems = true;
                    }
                    const auto& mat = m_materialList[static_cast<size_t>(i)];
                    bool selected = (i == m_selectedMaterialIdx);
                    if (ImGui::Selectable(mat.name.c_str(), selected)) {
                        m_selectedMaterialIdx = i;
                        m_materialName = mat.name;
                        m_materialSelected = true;
                        applyMachineToolpathDefaults();
                        markToolpathSettingsChanged();
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    } else {
        if (!m_materialMgr) {
            ImGui::TextColored(kYellow, "No material database available.");
        } else {
            ImGui::TextDisabled("No materials in library.");
        }
    }
    ImGui::TextWrapped(
        "Project material: this selection is saved with this carve operation. It does not "
        "change the Library model's shared material metadata.");

    // Show Janka hardness + machine profile if selected
    if (m_selectedMaterialIdx >= 0) {
        const auto& mat = m_materialList[static_cast<size_t>(m_selectedMaterialIdx)];
        if (mat.jankaHardness > 0.0f) {
            ImGui::SameLine();
            ImGui::TextDisabled("Janka: %.0f lbf", static_cast<double>(mat.jankaHardness));
        }
    }

    // Show which machine profile is driving the calculation
    {
        const auto& mp = Config::instance().getActiveMachineProfile();
        ImGui::TextDisabled("Machine: %s (%.0f RPM, %.0fW, %s, %.0f%% rigidity)",
                            mp.name.c_str(),
                            static_cast<double>(mp.spindleMaxRPM),
                            static_cast<double>(mp.spindlePower),
                            gcode::driveSystemDisplayName(mp.driveSystem),
                            gcode::effectiveRigidityFactor(mp) * 100.0);
        ImGui::TextDisabled("Machine defaults: feed %.0f, plunge %.0f, stepdown %.2f, rapid %.0f",
                            static_cast<double>(mp.defaultFeedRate),
                            static_cast<double>(mp.defaultPlungeRate),
                            static_cast<double>(mp.defaultStepdown),
                            static_cast<double>(mp.rapidRate));
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Suggested Starting Point");
    ImGui::TextWrapped(
        "These are suggestions, not safety guarantees. Machine defaults come from the "
        "active profile; tool-and-material guidance also considers hardness, cutter "
        "diameter, flute count, spindle power, and drive system.");

    if (ImGui::Button("Use Machine Defaults")) {
        applyMachineToolpathDefaults();
    }
    ImGui::SameLine();
    const bool canCalculate = m_selectedMaterialIdx >= 0 &&
                              m_toolPlan.finishingIntent().has_value();
    if (!canCalculate) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Calculate from Tool/Material")) {
        const auto& mat = m_materialList[static_cast<size_t>(m_selectedMaterialIdx)];
        applyMaterialToolpathRecommendation(mat);
    }
    if (!canCalculate) {
        ImGui::EndDisabled();
    }
    if (m_selectedMaterialIdx >= 0 && !m_toolPlan.finishingIntent()) {
        ImGui::TextDisabled(
            "Choose a tool next; tool-and-material feed guidance will then be available here.");
    }

    ImGui::TextDisabled("Current starting values: feed %.0f, plunge %.0f, stepdown %.2f",
                        static_cast<double>(m_toolpathConfig.feedRateMmMin),
                        static_cast<double>(m_toolpathConfig.plungeRateMmMin),
                        static_cast<double>(m_toolpathConfig.stepdownMm));

    // --- Feed rate inputs (wider, typeable) ---
    if (ImGui::CollapsingHeader("Advanced feed & toolpath settings")) {
        ImGui::TextWrapped(
            "Fine-tune these only when you understand the machine, tool, and material limits.");
        ImGui::SetNextItemWidth(iw);
        f32 prevFeed = m_toolpathConfig.feedRateMmMin;
        ImGui::InputFloat(
            "Feed Rate (mm/min)", &m_toolpathConfig.feedRateMmMin, 50.0f, 200.0f, "%.0f");
        m_toolpathConfig.feedRateMmMin =
            std::clamp(m_toolpathConfig.feedRateMmMin, 10.0f, 20000.0f);
        if (m_toolpathConfig.feedRateMmMin != prevFeed)
            markToolpathSettingsChanged();

        ImGui::SetNextItemWidth(iw);
        f32 prevPlunge = m_toolpathConfig.plungeRateMmMin;
        ImGui::InputFloat(
            "Plunge Rate (mm/min)", &m_toolpathConfig.plungeRateMmMin, 10.0f, 50.0f, "%.0f");
        m_toolpathConfig.plungeRateMmMin =
            std::clamp(m_toolpathConfig.plungeRateMmMin, 5.0f, 5000.0f);
        if (m_toolpathConfig.plungeRateMmMin != prevPlunge)
            markToolpathSettingsChanged();

        ImGui::SetNextItemWidth(iw);
        f32 prevStepdown = m_toolpathConfig.stepdownMm;
        ImGui::InputFloat("Stepdown (mm)", &m_toolpathConfig.stepdownMm, 0.1f, 0.5f, "%.2f");
        m_toolpathConfig.stepdownMm = std::clamp(m_toolpathConfig.stepdownMm, 0.1f, 50.0f);
        if (m_toolpathConfig.stepdownMm != prevStepdown)
            markToolpathSettingsChanged();

        ImGui::SetNextItemWidth(iw);
        f32 prevSafeZ = m_toolpathConfig.safeZMm;
        ImGui::InputFloat("Safe Z (mm)", &m_toolpathConfig.safeZMm, 0.5f, 2.0f, "%.1f");
        m_toolpathConfig.safeZMm = std::clamp(m_toolpathConfig.safeZMm, 1.0f, 50.0f);
        if (m_toolpathConfig.safeZMm != prevSafeZ)
            markToolpathSettingsChanged();

        ImGui::SetNextItemWidth(iw);
        const char* stepoverLabels[] = {
            "Ultra Fine (1%)", "Fine (8%)", "Basic (12%)", "Rough (25%)", "Roughing (40%)"};
        int stepIdx = static_cast<int>(m_toolpathConfig.stepoverPreset);
        if (ImGui::Combo("Stepover", &stepIdx, stepoverLabels, 5)) {
            m_toolpathConfig.stepoverPreset = static_cast<carve::StepoverPreset>(stepIdx);
            markToolpathSettingsChanged();
        }

        // Toolpath point resolution along scan lines
        ImGui::SetNextItemWidth(iw);
        if (m_toolpathConfig.scanResolutionMm <= 0.0f)
            m_toolpathConfig.scanResolutionMm = 1.0f;
        if (ImGui::SliderFloat(
                "Path Detail (mm)", &m_toolpathConfig.scanResolutionMm, 0.2f, 10.0f, "%.2f"))
            markToolpathSettingsChanged();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum point spacing along each scan line.\n"
                              "Actual spacing is capped by tool tip diameter and stepover.");

        ImGui::Spacing();
        ImGui::SeparatorText("Cut Area");

        ImGui::SetNextItemWidth(iw);
        const char* extentLabels[] = {"Model extents", "Material extents"};
        int extentsIdx = static_cast<int>(m_toolpathConfig.cutExtents);
        if (ImGui::Combo("Cut Extents", &extentsIdx, extentLabels, 2)) {
            m_toolpathConfig.cutExtents = static_cast<carve::CutExtents>(extentsIdx);
            markToolpathSettingsChanged();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Scan Pattern");

        ImGui::SetNextItemWidth(iw);
        const char* axisLabels[] = {"X Only", "Y Only", "X then Y", "Y then X"};
        int axisIdx = static_cast<int>(m_toolpathConfig.axis);
        if (ImGui::Combo("Scan Axis", &axisIdx, axisLabels, 4)) {
            m_toolpathConfig.axis = static_cast<carve::ScanAxis>(axisIdx);
            markToolpathSettingsChanged();
        }

        ImGui::SetNextItemWidth(iw);
        const char* dirLabels[] = {"Left/Up to Right/Down",
                                   "Right/Down to Left/Up",
                                   "Bidirectional"};
        int dirIdx = static_cast<int>(m_toolpathConfig.direction);
        if (ImGui::Combo("Mill Direction", &dirIdx, dirLabels, 3)) {
            m_toolpathConfig.direction = static_cast<carve::MillDirection>(dirIdx);
            markToolpathSettingsChanged();
        }
    }

    // Auto-confirm when material is selected
    if (!m_materialSelected && m_materialList.empty()) {
        ImGui::Spacing();
        if (ImGui::Checkbox("Confirm settings", &m_materialSelected)) {
            markToolpathSettingsChanged();
        }
    }
}

// --- renderPreview: fixed-depth raster preview, stats, controls ---

} // namespace dw

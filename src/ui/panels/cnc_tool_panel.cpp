#include "ui/panels/cnc_tool_panel.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "core/cnc/tool_calculator.h"
#include "core/cnc/machine_profile_calculator_adapter.h"
#include "core/config/config.h"
#include "core/database/tool_database.h"
#include "core/gcode/machine_profile.h"
#include "core/materials/material_manager.h"
#include "core/utils/log.h"
#include "ui/icons.h"
#include "ui/widgets/table_helpers.h"

namespace dw {

// Tool type display names (shared with tool_browser_panel.cpp)
static const char* toolTypeName(VtdbToolType type) {
    switch (type) {
    case VtdbToolType::BallNose: return "Ball Nose";
    case VtdbToolType::EndMill: return "End Mill";
    case VtdbToolType::Radiused: return "Radiused";
    case VtdbToolType::VBit: return "V-Bit";
    case VtdbToolType::TaperedBallNose: return "Tapered Ball Nose";
    case VtdbToolType::Drill: return "Drill";
    case VtdbToolType::ThreadMill: return "Thread Mill";
    case VtdbToolType::FormTool: return "Form Tool";
    case VtdbToolType::DiamondDrag: return "Diamond Drag";
    default: return "Unknown";
    }
}

static const char* driveTypeName(DriveType dt) {
    switch (dt) {
    case DriveType::Belt: return "Belt";
    case DriveType::LeadScrew: return "Lead Screw";
    case DriveType::BallScrew: return "Ball Screw";
    case DriveType::RackPinion: return "Rack & Pinion";
    }
    return "Unknown";
}

static const char* hardnessBandName(HardnessBand band) {
    switch (band) {
    case HardnessBand::Soft: return "Soft Wood";
    case HardnessBand::Medium: return "Medium Wood";
    case HardnessBand::Hard: return "Hard Wood";
    case HardnessBand::VeryHard: return "Very Hard Wood";
    case HardnessBand::Composite: return "Composite";
    case HardnessBand::Metal: return "Metal";
    case HardnessBand::Plastic: return "Plastic";
    }
    return "Unknown";
}

CncToolPanel::CncToolPanel() : Panel("Tool & Material") {}

void CncToolPanel::render() {
    if (!m_open) return;

    if (m_needsRefresh && m_toolDatabase) {
        loadData();
        m_needsRefresh = false;
    }

    applyMinSize(16, 8);
    if (!ImGui::Begin(m_title.c_str(), &m_open)) {
        ImGui::End();
        return;
    }

    if (!m_toolDatabase) {
        ImGui::TextDisabled("No tool database loaded");
        ImGui::End();
        return;
    }

    renderToolSelector();
    ImGui::Spacing();
    renderMaterialSelector();
    ImGui::Spacing();
    ImGui::Separator();
    renderMachineInfo();
    ImGui::Separator();

    // Auto-recalculate when selection changes
    bool selectionChanged = (m_selectedGeometryId != m_prevGeometryId) ||
                            (m_selectedMaterialId != m_prevMaterialId) ||
                            (m_selectedMachineId != m_prevMachineId);

    if (selectionChanged) {
        m_prevGeometryId = m_selectedGeometryId;
        m_prevMaterialId = m_selectedMaterialId;
        m_prevMachineId = m_selectedMachineId;

        if (!m_selectedGeometryId.empty() && m_selectedMaterialId > 0) {
            recalculate();
        } else {
            m_hasCalcResult = false;
        }
    }

    renderResults();

    ImGui::End();
}

void CncToolPanel::loadData() {
    if (!m_toolDatabase) return;

    m_treeEntries = m_toolDatabase->getAllTreeEntries();
    m_geometries = m_toolDatabase->findAllGeometries();
    m_machines = m_toolDatabase->findAllMachines();

    // Sort tree entries by sibling_order
    std::sort(m_treeEntries.begin(), m_treeEntries.end(),
              [](const VtdbTreeEntry& a, const VtdbTreeEntry& b) {
                  return a.sibling_order < b.sibling_order;
              });

    // Load materials from MaterialManager (has Janka hardness)
    if (m_materialManager) {
        m_materials = m_materialManager->getAllMaterials();
    }

    // Auto-select first machine if none selected
    if (m_selectedMachineId.empty() && !m_machines.empty()) {
        m_selectedMachineId = m_machines[0].id;
    }
}

void CncToolPanel::renderToolSelector() {
    ImGui::SeparatorText("Tool");

    // Build display label for current selection
    const char* previewLabel = m_selectedToolName.empty()
                                   ? "(Select a tool)"
                                   : m_selectedToolName.c_str();

    if (ImGui::BeginCombo("##ToolSelect", previewLabel)) {
        for (const auto& entry : m_treeEntries) {
            // Only show leaf entries (tools, not groups)
            if (entry.tool_geometry_id.empty()) continue;

            // Find matching geometry for display details
            const VtdbToolGeometry* geom = nullptr;
            for (const auto& g : m_geometries) {
                if (g.id == entry.tool_geometry_id) {
                    geom = &g;
                    break;
                }
            }

            // Build display string
            char label[256];
            if (geom) {
                const char* unitSuffix = (geom->units == VtdbUnits::Metric) ? "mm" : "\"";
                std::snprintf(label, sizeof(label), "%s %s (%.4f%s, %dF)",
                              toolTypeName(geom->tool_type),
                              entry.name.c_str(),
                              geom->diameter, unitSuffix,
                              geom->num_flutes);
            } else {
                std::snprintf(label, sizeof(label), "%s", entry.name.c_str());
            }

            bool selected = (entry.tool_geometry_id == m_selectedGeometryId);
            if (ImGui::Selectable(label, selected)) {
                m_selectedGeometryId = entry.tool_geometry_id;
                m_selectedToolName = label;
            }
        }
        ImGui::EndCombo();
    }

    // Show selected tool summary
    if (!m_selectedGeometryId.empty()) {
        for (const auto& g : m_geometries) {
            if (g.id == m_selectedGeometryId) {
                const char* unitSuffix = (g.units == VtdbUnits::Metric) ? "mm" : "in";
                ImGui::TextDisabled("%s  |  %.4f %s  |  %d flute(s)",
                                    toolTypeName(g.tool_type),
                                    g.diameter, unitSuffix,
                                    g.num_flutes);
                break;
            }
        }
    }
}

void CncToolPanel::renderMaterialSelector() {
    ImGui::SeparatorText("Material");

    const char* previewLabel = m_selectedMaterialName.empty()
                                   ? "(Select a material)"
                                   : m_selectedMaterialName.c_str();

    if (ImGui::BeginCombo("##MaterialSelect", previewLabel)) {
        for (const auto& mat : m_materials) {
            if (mat.isHidden) continue;

            char label[256];
            std::snprintf(label, sizeof(label), "%s (%s)",
                          mat.name.c_str(),
                          materialCategoryToString(mat.category).c_str());

            bool selected = (mat.id == m_selectedMaterialId);
            if (ImGui::Selectable(label, selected)) {
                m_selectedMaterialId = mat.id;
                m_selectedMaterialName = mat.name;
                m_selectedJanka = mat.jankaHardness;
                m_selectedMaterialNameForCalc = mat.name;
            }
        }
        ImGui::EndCombo();
    }

    // Show selected material summary
    if (m_selectedMaterialId > 0) {
        HardnessBand band = ToolCalculator::classifyMaterial(
            m_selectedJanka, m_selectedMaterialNameForCalc);
        ImGui::TextDisabled("Janka: %.0f lbf  |  %s",
                            static_cast<double>(m_selectedJanka), hardnessBandName(band));
    }
}

void CncToolPanel::renderMachineInfo() {
    ImGui::Spacing();

    if (m_machines.size() > 1) {
        // Machine selector combo
        std::string machPreview = "(Default)";
        for (const auto& mach : m_machines) {
            if (mach.id == m_selectedMachineId) {
                machPreview = mach.name;
                break;
            }
        }

        if (ImGui::BeginCombo("Machine##CncTool", machPreview.c_str())) {
            for (const auto& mach : m_machines) {
                bool selected = (mach.id == m_selectedMachineId);
                std::string label = mach.name;
                if (!mach.make.empty()) label += " (" + mach.make + ")";
                if (ImGui::Selectable(label.c_str(), selected)) {
                    m_selectedMachineId = mach.id;
                }
            }
            ImGui::EndCombo();
        }
    }

    // Show machine info line
    bool found = false;
    for (const auto& mach : m_machines) {
        if (mach.id == m_selectedMachineId) {
            ImGui::TextDisabled("%s  |  %s  |  %d RPM  |  %.0f W",
                                mach.name.c_str(),
                                driveTypeName(mach.drive_type),
                                mach.max_rpm,
                                mach.spindle_power_watts);
            found = true;
            break;
        }
    }
    if (!found) {
        const auto& mp = Config::instance().getActiveMachineProfile();
        ImGui::TextDisabled("Using profile: %s  |  %.0f RPM  |  %.0f W",
                            mp.name.c_str(),
                            static_cast<double>(mp.spindleMaxRPM),
                            static_cast<double>(mp.spindlePower));
    }

    ImGui::Spacing();
}

void CncToolPanel::recalculate() {
    // Find selected geometry
    const VtdbToolGeometry* geom = nullptr;
    for (const auto& g : m_geometries) {
        if (g.id == m_selectedGeometryId) {
            geom = &g;
            break;
        }
    }
    if (!geom) {
        m_hasCalcResult = false;
        return;
    }

    // Build calculator input
    CalcInput input;
    input.diameter = geom->diameter;
    input.num_flutes = geom->num_flutes;
    input.tool_type = geom->tool_type;
    input.units = geom->units;
    input.janka_hardness = m_selectedJanka;
    input.material_name = m_selectedMaterialNameForCalc;

    // Get machine parameters from .vtdb machine if selected
    bool foundMachine = false;
    for (const auto& mach : m_machines) {
        if (mach.id == m_selectedMachineId) {
            input.spindle_power_watts = mach.spindle_power_watts;
            input.max_rpm = mach.max_rpm;
            input.drive_type = mach.drive_type;
            foundMachine = true;
            break;
        }
    }

    // Fall back to active Config machine profile
    if (!foundMachine) {
        const auto& mp = Config::instance().getActiveMachineProfile();
        applyMachineProfileToCalcInput(mp, input);
    }

    m_calcResult = ToolCalculator::calculate(input);
    m_hasCalcResult = true;

    log::debugf("CncToolPanel", "Calculated: RPM=%d Feed=%.1f Plunge=%.1f",
                m_calcResult.rpm, m_calcResult.feed_rate, m_calcResult.plunge_rate);
}

void CncToolPanel::renderResults() {
    ImGui::Spacing();

    if (!m_hasCalcResult) {
        ImGui::TextDisabled("Select a tool and material to see recommended parameters");
        return;
    }

    ImGui::SeparatorText("Recommended Parameters");

    // Classification info
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s",
                       hardnessBandName(m_calcResult.hardness_band));
    ImGui::SameLine();
    ImGui::TextDisabled("| Rigidity: %.0f%%", m_calcResult.rigidity_factor * 100.0);

    ImGui::Spacing();

    // Determine units
    bool isMetric = false;
    for (const auto& g : m_geometries) {
        if (g.id == m_selectedGeometryId) {
            isMetric = (g.units == VtdbUnits::Metric);
            break;
        }
    }
    const char* unit = isMetric ? "mm" : "in";
    const char* feedUnit = isMetric ? "mm/min" : "in/min";

    // Main parameters with green highlighting
    ImVec4 valueColor(0.4f, 0.8f, 0.4f, 1.0f);

    if (auto t = LabelValueTable("##toolparams")) {
        t.rowfColored("RPM:", valueColor, "%d", m_calcResult.rpm);
        t.rowfColored("Feed Rate:", valueColor, "%.1f %s", m_calcResult.feed_rate, feedUnit);
        t.rowfColored("Plunge Rate:", valueColor, "%.1f %s", m_calcResult.plunge_rate, feedUnit);
        t.rowfColored("Stepdown:", valueColor, "%.4f %s", m_calcResult.stepdown, unit);
        t.rowfColored("Stepover:", valueColor, "%.4f %s", m_calcResult.stepover, unit);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Secondary info
    ImGui::TextDisabled("Chip Load: %.4f %s/tooth", m_calcResult.chip_load, unit);

    if (m_calcResult.power_required > 0.0) {
        ImGui::TextDisabled("Power: %.0f W", m_calcResult.power_required);
        if (m_calcResult.power_limited) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "(power limited)");
        }
    }
}

} // namespace dw

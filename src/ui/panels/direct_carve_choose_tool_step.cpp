// Direct Carve: Choose Tool planning step.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>
#include <utility>

#include <imgui.h>

#include "core/database/tool_database.h"
#include "core/database/toolbox_repository.h"
#include "ui/icons.h"
#include "ui/panels/direct_carve_layout_policy.h"
#include "ui/tool_library_access.h"
#include "ui/ui_colors.h"

namespace dw {
namespace {
constexpr auto& kGreen = colors::kSuccess;
constexpr auto& kYellow = colors::kWarning;

f64 diameterMm(const VtdbToolGeometry& geometry) {
    return geometry.units == VtdbUnits::Imperial ? geometry.diameter * 25.4 : geometry.diameter;
}

const char* toolTypeLabel(VtdbToolType type) {
    switch (type) {
    case VtdbToolType::BallNose:
        return "Ball Nose";
    case VtdbToolType::TaperedBallNose:
        return "TBN";
    case VtdbToolType::VBit:
        return "V-Bit";
    case VtdbToolType::EndMill:
        return "End Mill";
    case VtdbToolType::Radiused:
        return "Radiused";
    default:
        return "Tool";
    }
}

} // namespace

void DirectCarvePanel::renderToolSelect() {
    if (!renderPreparationStepGuidance(carve_preparation::PreparationStageId::ChooseTool)) {
        ImGui::TextUnformatted("Choose Tool");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Choose a finishing tool for detail, then optionally choose a separate clearing tool. "
            "Tool options are suggestions; smaller finishing tips capture finer detail but create "
            "longer carving paths.");
    }
    // Load tools from database on first visit
    if (!m_toolLibraryLoaded && m_toolDb) {
        m_toolLibraryLoaded = true;
        m_toolboxTools.clear();
        m_allTools.clear();
        auto isCarveType = [](VtdbToolType t) {
            return t == VtdbToolType::BallNose || t == VtdbToolType::TaperedBallNose ||
                   t == VtdbToolType::VBit || t == VtdbToolType::EndMill ||
                   t == VtdbToolType::Radiused;
        };
        // Load toolbox subset
        if (m_toolboxRepo) {
            auto ids = m_toolboxRepo->getAllGeometryIds();
            for (const auto& id : ids) {
                auto geom = m_toolDb->findGeometryById(id);
                if (geom && isCarveType(geom->tool_type))
                    m_toolboxTools.push_back(std::move(*geom));
            }
        }
        // Load full library
        auto allGeoms = m_toolDb->findAllGeometries();
        for (auto& g : allGeoms) {
            if (isCarveType(g.tool_type))
                m_allTools.push_back(std::move(g));
        }
        // Default to toolbox if it has tools, otherwise show all
        m_showAllTools = m_toolboxTools.empty();
        m_libraryTools = m_showAllTools ? m_allTools : m_toolboxTools;
    }
    ImGui::TextUnformatted("Choose for");
    const bool choosingFinishing = m_toolPickerRole == carve::DirectCarveToolPickerRole::Finishing;
    if (ImGui::RadioButton("Finishing / detail (required)##ToolRole", choosingFinishing)) {
        m_toolPickerRole = carve::DirectCarveToolPickerRole::Finishing;
        m_toolSelectionMessage.clear();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Clearing (optional)##ToolRole", !choosingFinishing)) {
        m_toolPickerRole = carve::DirectCarveToolPickerRole::Clearing;
        m_toolSelectionMessage.clear();
    }
    ImGui::TextDisabled("Pick a role, then choose its tool below. Each role keeps its own choice.");
    ImGui::TextUnformatted("Clearing pass");
    const auto clearingMode = m_toolPlan.clearingMode();
    const auto setClearingMode = [&](carve::ClearingToolMode mode) {
        if (m_toolPlan.clearingMode() == mode)
            return;
        m_toolPlan.setClearingMode(mode);
        m_toolSetupConfirmed = false;
        m_toolSelectionMessage.clear();
        if (mode == carve::ClearingToolMode::Selected && !m_toolPlan.clearingIntent()) {
            m_toolSelectionMessage = "Choose a flat End Mill or Radiused tool for clearing.";
        }
        markToolPlanChanged();
    };
    if (ImGui::RadioButton("Automatic##ClearingMode",
                           clearingMode == carve::ClearingToolMode::Automatic)) {
        setClearingMode(carve::ClearingToolMode::Automatic);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Choose a tool##ClearingMode",
                           clearingMode == carve::ClearingToolMode::Selected)) {
        m_toolPickerRole = carve::DirectCarveToolPickerRole::Clearing;
        setClearingMode(carve::ClearingToolMode::Selected);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("No clearing pass##ClearingMode",
                           clearingMode == carve::ClearingToolMode::Disabled)) {
        setClearingMode(carve::ClearingToolMode::Disabled);
    }
    ImGui::TextDisabled(
        "Automatic may suggest a larger flat tool at Preview; No clearing pass uses detail only.");
    if (m_toolPlan.clearingMode() == carve::ClearingToolMode::Selected &&
        !m_toolPlan.clearingIntent()) {
        ImGui::TextColored(kYellow,
                           "Choose a flat End Mill or Radiused tool for the clearing role.");
    }
    ImGui::Spacing();
    bool hasLibrary = !m_libraryTools.empty();
    if (hasLibrary || !m_allTools.empty()) {
        if (!m_materialName.empty()) {
            ImGui::TextDisabled("Material context: %s", m_materialName.c_str());
            ImGui::Spacing();
        }
        if (ImGui::BeginTabBar("##toolSource")) {
            if (ImGui::BeginTabItem("Tool Library")) {
                m_useManualTool = false;
                ImGui::Spacing();
                bool hasToolboxTools = !m_toolboxTools.empty();
                if (hasToolboxTools) {
                    bool showAll = m_showAllTools;
                    if (ImGui::RadioButton("My Toolbox", !showAll)) {
                        m_showAllTools = false;
                        m_libraryTools = m_toolboxTools;
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("All Tools", showAll)) {
                        m_showAllTools = true;
                        m_libraryTools = m_allTools;
                    }
                } else {
                    ImGui::TextDisabled("Showing all tools (My Toolbox is empty)");
                }

                ImGui::SameLine();
                const float editButtonWidth = ImGui::CalcTextSize("Edit Toolbox").x +
                                              ImGui::GetStyle().FramePadding.x * 2.0F;
                const float editButtonRoom = ImGui::GetContentRegionAvail().x;
                if (editButtonRoom > editButtonWidth) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + editButtonRoom - editButtonWidth);
                }
                if (ImGui::SmallButton("Edit Toolbox")) {
                    if (m_openToolBrowser)
                        m_openToolBrowser();
                    ImGui::SetWindowFocus(kToolLibraryWindowTitle);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", kToolLibraryStatusTooltip);

                ImGui::Spacing();
                renderToolLibraryPicker();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Manual Entry")) {
                m_useManualTool = true;
                ImGui::Spacing();
                renderManualToolEntry();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    } else {
        if (!m_toolDb) {
            ImGui::TextColored(kYellow, "No tool database connected.");
        } else if (!m_materialName.empty()) {
            ImGui::TextDisabled(
                "Material: %s | Tool Library is empty; enter a tool manually below.",
                m_materialName.c_str());
        } else {
            ImGui::TextDisabled("Tool library is empty. Import tools via Tool Library.");
        }
        m_useManualTool = true;
        renderManualToolEntry();
    }
    if (!m_toolSelectionMessage.empty()) {
        const bool isError = m_toolSelectionMessage != "Finishing tool selected." &&
                             m_toolSelectionMessage != "Clearing tool selected.";
        ImGui::TextColored(isError ? kYellow : kGreen, "%s", m_toolSelectionMessage.c_str());
    }

    const auto& finishing = m_toolPlan.finishingIntent();
    const bool hasRealFinishingTool = finishing && carve::isUsableDirectCarveTool(*finishing);
    if (!hasRealFinishingTool)
        m_toolSetupConfirmed = false;
    if (hasRealFinishingTool) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(kGreen,
                           "%s Finishing / detail: %s  %.3gmm  %d flute%s",
                           Icons::Check,
                           toolTypeLabel(finishing->tool_type),
                           diameterMm(*finishing),
                           finishing->num_flutes,
                           finishing->num_flutes != 1 ? "s" : "");

        bool clearingComplete = true;
        switch (m_toolPlan.clearingMode()) {
        case carve::ClearingToolMode::Automatic:
            ImGui::TextDisabled("Clearing: Automatic suggestion at Preview");
            break;
        case carve::ClearingToolMode::Disabled:
            ImGui::TextDisabled("Clearing: No clearing pass");
            break;
        case carve::ClearingToolMode::Selected:
            clearingComplete = m_toolPlan.selectionComplete();
            if (clearingComplete) {
                const auto& clearing = *m_toolPlan.clearingIntent();
                ImGui::TextColored(kGreen,
                                   "%s Clearing: %s  %.3gmm  %d flute%s",
                                   Icons::Check,
                                   toolTypeLabel(clearing.tool_type),
                                   diameterMm(clearing),
                                   clearing.num_flutes,
                                   clearing.num_flutes != 1 ? "s" : "");
            } else {
                m_toolSetupConfirmed = false;
                ImGui::TextColored(kYellow, "Clearing: choose a flat End Mill or Radiused tool.");
            }
            break;
        }

        ImGui::Spacing();
        if (!clearingComplete)
            ImGui::BeginDisabled();
        if (ImGui::Checkbox("Tool plan reviewed", &m_toolSetupConfirmed)) {
            clearFinalConfirmation();
        }
        if (!clearingComplete)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Review both roles. If clearing is generated, it runs first and the machine "
                "prompts for the finishing-tool change.");
        }
        ImGui::TextDisabled(
            "Why this matters: tip size sets attainable detail and strongly affects runtime.");
    }
}

void DirectCarvePanel::renderManualToolEntry() {
    float iw = ImGui::GetFontSize() * 8.0f;

    const char* typeNames[] = {"Ball Nose", "V-Bit", "End Mill", "Tapered Ball Nose", "Radiused"};
    const auto normalizeManualValues = [&]() {
        m_manualDiameter = std::clamp(m_manualDiameter, 0.1f, 50.0f);
        m_manualFlutes = std::clamp(m_manualFlutes, 1, 8);
        m_manualAngle = std::clamp(m_manualAngle, 10.0f, 180.0f);
        const float halfDia = m_manualDiameter * 0.5f;
        m_manualTipRadius = std::clamp(m_manualTipRadius, 0.05f, halfDia);
    };
    const auto acceptTool = [&]() {
        normalizeManualValues();
        const VtdbToolType types[] = {VtdbToolType::BallNose,
                                      VtdbToolType::VBit,
                                      VtdbToolType::EndMill,
                                      VtdbToolType::TaperedBallNose,
                                      VtdbToolType::Radiused};
        const auto tool = carve::makeDirectCarveManualTool(types[m_manualToolType],
                                                           static_cast<f64>(m_manualDiameter),
                                                           m_manualFlutes,
                                                           static_cast<f64>(m_manualAngle),
                                                           static_cast<f64>(m_manualTipRadius));
        const auto result = m_toolPlan.selectTool(m_toolPickerRole, tool);
        m_toolSelectionMessage =
            result ? (m_toolPickerRole == carve::DirectCarveToolPickerRole::Finishing
                          ? "Finishing tool selected."
                          : "Clearing tool selected.")
                   : result.message;
        if (result) {
            m_toolSetupConfirmed = false;
            markToolPlanChanged();
        }
    };
    const auto renderAcceptButton = [&](ImVec2 size = {}) {
        const bool isClearing = m_toolPickerRole == carve::DirectCarveToolPickerRole::Clearing;
        const bool clearingTypeSupported = m_manualToolType == 2 || m_manualToolType == 4;
        const bool canAccept = m_manualDiameter > 0.0f && (!isClearing || clearingTypeSupported);
        if (!canAccept)
            ImGui::BeginDisabled();
        if (ImGui::Button("Use This Tool", size))
            acceptTool();
        if (!canAccept)
            ImGui::EndDisabled();
        if (isClearing && !clearingTypeSupported &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Clearing requires a flat End Mill or Radiused tool.");
        }
    };

    const bool compact = directCarveManualToolUsesCompactRow(ImGui::GetContentRegionAvail().x,
                                                             ImGui::GetFontSize());
    if (compact &&
        ImGui::BeginTable("##CompactManualTool",
                          5,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthStretch, 1.35F);
        ImGui::TableSetupColumn("diameter", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("flutes", ImGuiTableColumnFlags_WidthStretch, 0.7F);
        ImGui::TableSetupColumn("detail", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch, 1.1F);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Tool type");
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Diameter mm");
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Flutes");
        ImGui::TableNextColumn();
        if (m_manualToolType == 1)
            ImGui::TextDisabled("Angle deg");
        else if (m_manualToolType == 0 || m_manualToolType == 3 || m_manualToolType == 4)
            ImGui::TextDisabled("Tip radius mm");
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Apply");

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::Combo("##ManualToolType", &m_manualToolType, typeNames, 5);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputFloat("##ManualDiameter", &m_manualDiameter, 0.1F, 1.0F, "%.3f");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputInt("##ManualFlutes", &m_manualFlutes);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0F);
        if (m_manualToolType == 1) {
            ImGui::InputFloat("##ManualAngle", &m_manualAngle, 1.0F, 10.0F, "%.1f");
        } else if (m_manualToolType == 0 || m_manualToolType == 3 || m_manualToolType == 4) {
            ImGui::InputFloat("##ManualTipRadius", &m_manualTipRadius, 0.1F, 0.5F, "%.3f");
        } else {
            ImGui::TextDisabled("Not needed");
        }
        ImGui::TableNextColumn();
        renderAcceptButton({-1.0F, 0.0F});
        ImGui::EndTable();
    } else {
        ImGui::SetNextItemWidth(iw);
        ImGui::Combo("Tool Type", &m_manualToolType, typeNames, 5);

        ImGui::SetNextItemWidth(iw);
        ImGui::InputFloat("Diameter (mm)", &m_manualDiameter, 0.1f, 1.0f, "%.3f");
        // Keep the required novice action beside the primary diameter field.
        // If a table is clipped by a very short body, this fallback must still
        // expose the action before optional geometry details continue below.
        ImGui::SameLine();
        renderAcceptButton({ImGui::GetFontSize() * 9.0F, 0.0F});

        ImGui::SetNextItemWidth(iw);
        ImGui::InputInt("Flutes", &m_manualFlutes);

        if (m_manualToolType == 1) {
            ImGui::SetNextItemWidth(iw);
            ImGui::InputFloat("Included Angle (deg)", &m_manualAngle, 1.0f, 10.0f, "%.1f");
        }
        if (m_manualToolType == 0 || m_manualToolType == 3 || m_manualToolType == 4) {
            ImGui::SetNextItemWidth(iw);
            ImGui::InputFloat("Tip Radius (mm)", &m_manualTipRadius, 0.1f, 0.5f, "%.3f");
        }
    }

    const bool clearingTypeSupported = m_manualToolType == 2 || m_manualToolType == 4;
    if (m_toolPickerRole == carve::DirectCarveToolPickerRole::Clearing && !clearingTypeSupported) {
        ImGui::TextColored(kYellow, "Clearing requires a flat End Mill or Radiused tool.");
    }
    normalizeManualValues();
}

} // namespace dw

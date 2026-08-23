// Direct Carve tool-library table and role-aware row selection.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>
#include <string>

#include <imgui.h>

#include "ui/panels/direct_carve_layout_policy.h"
#include "ui/theme.h"

namespace dw {
namespace {

f64 diameterMm(const VtdbToolGeometry& tool) {
    return tool.units == VtdbUnits::Imperial ? tool.diameter * 25.4 : tool.diameter;
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

ImVec4 toolTypeColor(VtdbToolType type) {
    ImU32 color = Theme::Colors::Secondary;
    switch (type) {
    case VtdbToolType::BallNose:
        color = Theme::Colors::Primary;
        break;
    case VtdbToolType::TaperedBallNose:
        color = Theme::Colors::Warning;
        break;
    case VtdbToolType::VBit:
        color = Theme::Colors::Success;
        break;
    case VtdbToolType::EndMill:
        color = Theme::Colors::Error;
        break;
    case VtdbToolType::Radiused:
        color = Theme::Colors::Success;
        break;
    default:
        break;
    }
    return ImGui::ColorConvertU32ToFloat4(color);
}

} // namespace

void DirectCarvePanel::renderToolLibraryPicker() {
    const float rowHeight = ImGui::GetFrameHeight();
    const float belowListReserve = ImGui::GetFrameHeightWithSpacing() * 5.0F;
    const auto layout = chooseDirectCarveToolListLayout(ImGui::GetContentRegionAvail().y,
                                                        m_libraryTools.size(),
                                                        rowHeight,
                                                        ImGui::GetFrameHeight(),
                                                        belowListReserve,
                                                        ImGui::GetStyle().ChildBorderSize);

    ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
                                 ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (layout.scrolls)
        tableFlags |= ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable(
            "##DirectCarveToolList", 3, tableFlags, ImVec2(0.0F, std::max(1.0F, layout.height)))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Tool", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.0F);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0F);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(m_libraryTools.size()), rowHeight);
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const auto& tool = m_libraryTools[static_cast<std::size_t>(index)];
            const std::string identity = carve::stableDirectCarveToolIdentity(tool);
            const std::string name = resolveToolNameFormat(tool);
            const auto& activeIntent = m_toolPickerRole ==
                                               carve::DirectCarveToolPickerRole::Finishing
                                           ? m_toolPlan.finishingIntent()
                                           : m_toolPlan.clearingIntent();
            const bool selected =
                activeIntent &&
                (m_toolPickerRole == carve::DirectCarveToolPickerRole::Finishing ||
                 m_toolPlan.clearingMode() == carve::ClearingToolMode::Selected) &&
                carve::sameDirectCarveToolIdentity(*activeIntent, tool);
            const bool supported = m_toolPickerRole != carve::DirectCarveToolPickerRole::Clearing ||
                                   carve::isSupportedClearingTool(tool);

            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(identity.c_str());
            if (!supported) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }
            const float nameWidth = ImGui::GetContentRegionAvail().x;
            const ImVec2 namePosition = ImGui::GetCursorScreenPos();
            const ImGuiSelectableFlags selectableFlags =
                ImGuiSelectableFlags_SpanAllColumns |
                (supported ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled);
            const bool clicked = ImGui::Selectable(
                "##SelectTool", selected, selectableFlags, ImVec2(0.0F, rowHeight));
            const bool rowHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            ImGui::SetCursorScreenPos(namePosition);
            ImGui::TextUnformatted(name.c_str());
            if (clicked) {
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
            }
            if (!supported)
                ImGui::PopStyleColor();

            const bool nameClipped = ImGui::CalcTextSize(name.c_str()).x > nameWidth;
            if (rowHovered && (nameClipped || !supported)) {
                if (supported) {
                    ImGui::SetTooltip("%s", name.c_str());
                } else {
                    ImGui::SetTooltip("%s\nClearing requires a flat End Mill or Radiused tool.",
                                      name.c_str());
                }
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(toolTypeColor(tool.tool_type), "%s", toolTypeLabel(tool.tool_type));
            ImGui::TableSetColumnIndex(2);
            if (tool.tool_type == VtdbToolType::VBit && tool.included_angle > 0.0) {
                ImGui::TextDisabled("%.3gmm / %.0f deg", diameterMm(tool), tool.included_angle);
            } else {
                ImGui::TextDisabled("%.3gmm", diameterMm(tool));
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

} // namespace dw

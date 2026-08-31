#include "project_context_bar.h"

#include <string>
#include <utility>

#include <imgui.h>

#include "ui/theme.h"

namespace dw {
namespace {

const workshop::ProjectShellSnapshot& emptySnapshot() {
    static const workshop::ProjectShellSnapshot snapshot(
        {}, {}, workshop::MachineStatusSnapshot{"Machine offline", false, false});
    return snapshot;
}

struct ContextBarWidths {
    float project = 0.0F;
    float context = 0.0F;
    float action = 0.0F;
    float available = 0.0F;
    float gutter = 0.0F;
};

float labelWidth(const char* label) {
    return ImGui::CalcTextSize(label).x;
}

float labelWidth(const std::string& label) {
    return ImGui::CalcTextSize(label.c_str()).x;
}

ContextBarWidths measureContextBar(
    const workshop::ProjectContextBarPresentation& presentation) {
    const auto& style = ImGui::GetStyle();
    const float itemGap = style.ItemSpacing.x;
    ContextBarWidths widths;
    widths.project = labelWidth("PROJECT") + itemGap + labelWidth(presentation.projectLabel);
    if (presentation.projectDirty)
        widths.project += itemGap + labelWidth("Unsaved");

    widths.context = labelWidth("AREA") + itemGap + labelWidth(presentation.stageLabel) +
                     itemGap + labelWidth("/") + itemGap + labelWidth(presentation.focusLabel);
    widths.action = labelWidth(presentation.machineLabel);
    if (presentation.showBackToProject) {
        widths.action += itemGap + labelWidth("Back to Project") +
                         style.FramePadding.x * 2.0F;
    }

    const auto* viewport = ImGui::GetMainViewport();
    widths.available = viewport->WorkSize.x - style.WindowPadding.x * 2.0F;
    widths.gutter = style.CellPadding.x * 2.0F;
    return widths;
}

int contextBarRows(const workshop::ProjectContextBarPresentation& presentation) {
    const auto widths = measureContextBar(presentation);
    if (!workshop::projectContextBarUsesTwoRows(widths.available,
                                                widths.project,
                                                widths.context,
                                                widths.action,
                                                widths.gutter)) {
        return 1;
    }
    return widths.project + widths.action + widths.gutter <= widths.available ? 2 : 3;
}

ImVec4 machineColor(const workshop::MachineStatusSnapshot& machine) {
    const ImU32 color = machine.running
                            ? Theme::Colors::Warning
                            : (machine.connected ? Theme::Colors::Success : Theme::Colors::TextDim);
    return ImGui::ColorConvertU32ToFloat4(color);
}

} // namespace

void ProjectContextBar::setSnapshot(workshop::ProjectShellSnapshot snapshot) {
    m_snapshot = std::move(snapshot);
}

void ProjectContextBar::setBackToProjectCallback(BackToProjectCallback callback) {
    m_backToProject = std::move(callback);
}

float ProjectContextBar::height() const {
    const auto& snapshot = m_snapshot ? *m_snapshot : emptySnapshot();
    const auto presentation = workshop::projectContextBarPresentation(snapshot);
    const int rows = contextBarRows(presentation);
    return ImGui::GetFrameHeight() * static_cast<float>(rows) +
           ImGui::GetStyle().ItemSpacing.y * static_cast<float>(rows - 1) +
           ImGui::GetStyle().WindowPadding.y * 2.0F;
}

void ProjectContextBar::render() {
    const auto& snapshot = m_snapshot ? *m_snapshot : emptySnapshot();
    const auto& context = snapshot.context();
    const auto& machine = snapshot.machineStatus();
    const auto presentation = workshop::projectContextBarPresentation(snapshot);
    const auto widths = measureContextBar(presentation);
    const auto oneRowColumns = workshop::projectContextBarOneRowColumns(
        widths.available, widths.project, widths.action, widths.gutter);
    const int rows = contextBarRows(presentation);
    auto* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize({viewport->WorkSize.x, height()});
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImGui::ColorConvertU32ToFloat4(Theme::Colors::Surface));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(Theme::Colors::Border));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("##ProjectContextBar", nullptr, flags)) {
        const auto renderProject = [&]() {
            ImGui::TextDisabled("PROJECT");
            ImGui::SameLine();
            ImGui::TextUnformatted(presentation.projectLabel.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", presentation.projectLabel.c_str());
            if (presentation.projectDirty) {
                ImGui::SameLine();
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme::Colors::Warning),
                                   "Unsaved");
            }
        };
        const auto renderContext = [&]() {
            ImGui::TextDisabled("AREA");
            ImGui::SameLine();
            ImGui::TextUnformatted(presentation.stageLabel.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("/");
            ImGui::SameLine();
            ImGui::TextUnformatted(presentation.focusLabel.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", presentation.focusLabel.c_str());
        };
        const auto renderActions = [&]() {
            ImGui::TextColored(machineColor(machine), "%s", presentation.machineLabel.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", presentation.machineLabel.c_str());
            if (presentation.showBackToProject) {
                ImGui::SameLine();
                const bool alreadyInProject = context.route == workshop::WorkshopRoute::Project &&
                                              !context.libraryPreview.has_value();
                if (!presentation.enableBackToProject)
                    ImGui::BeginDisabled();
                if (ImGui::SmallButton("Back to Project") && m_backToProject)
                    m_backToProject();
                if (!presentation.enableBackToProject)
                    ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (presentation.runLocked)
                        ImGui::SetTooltip("Finish or stop Run CNC before returning");
                    else if (alreadyInProject)
                        ImGui::SetTooltip("You are already in this project");
                    else
                        ImGui::SetTooltip("Return without losing project context");
                }
            }
        };

        const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchProp |
                                           ImGuiTableFlags_NoSavedSettings;
        if (rows == 1 && ImGui::BeginTable("##ProjectContext", 3, tableFlags)) {
            ImGui::TableSetupColumn("Project",
                                    ImGuiTableColumnFlags_WidthFixed,
                                    oneRowColumns.project);
            ImGui::TableSetupColumn("Context",
                                    ImGuiTableColumnFlags_WidthStretch,
                                    1.0F);
            ImGui::TableSetupColumn("Actions",
                                    ImGuiTableColumnFlags_WidthFixed,
                                    oneRowColumns.action);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            renderProject();

            ImGui::TableSetColumnIndex(1);
            renderContext();

            ImGui::TableSetColumnIndex(2);
            renderActions();

            ImGui::EndTable();
        } else if (rows == 2) {
            if (ImGui::BeginTable("##ProjectContextCompact", 2, tableFlags)) {
                ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Actions",
                                        ImGuiTableColumnFlags_WidthFixed,
                                        widths.action);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                renderProject();
                ImGui::TableSetColumnIndex(1);
                renderActions();
                ImGui::EndTable();
            }
            renderContext();
        } else {
            renderProject();
            renderContext();
            renderActions();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

} // namespace dw

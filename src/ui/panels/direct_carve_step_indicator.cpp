// Responsive Direct Carve step tracker.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "ui/icons.h"
#include "ui/panels/direct_carve_step_indicator_policy.h"
#include "ui/ui_colors.h"

namespace dw {
namespace {

using carve_preparation::PreparationStageId;
using carve_preparation::PreparationStageState;

constexpr auto& kGreen = colors::kSuccess;
constexpr auto& kYellow = colors::kWarning;
constexpr auto& kDimmed = colors::kDimmed;

const char* navigationStateLabel(bool satisfied,
                                 bool current,
                                 bool reachable) noexcept {
    if (satisfied) return "Complete";
    if (current) return "Current";
    return reachable ? "Available" : "Locked";
}

const char* navigationStateIcon(bool satisfied,
                                bool current,
                                bool reachable) noexcept {
    if (satisfied) return Icons::Check;
    if (current) return Icons::ArrowRight;
    return reachable ? "" : Icons::Lock;
}

} // namespace

void DirectCarvePanel::renderStepIndicator() {
    refreshPinnedPreparation();
    const int currentIndex = static_cast<int>(m_currentStep);
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    float widestLabel = 0.0f;
    for (int index = 0; index < STEP_COUNT; ++index) {
        widestLabel = std::max(
            widestLabel,
            ImGui::CalcTextSize(stepLabel(static_cast<Step>(index))).x);
    }
    const bool compact = directCarveStepIndicatorNeedsCompactLayout(
        availableWidth,
        widestLabel,
        STEP_COUNT,
        ImGui::GetStyle().ItemSpacing.x);
    if (compact) {
        const bool complete = isStepSatisfied(m_currentStep);
        ImGui::TextWrapped("%s Step %d of %d: %s - %s",
                           navigationStateIcon(complete, true, true),
                           currentIndex + 1,
                           STEP_COUNT,
                           stepLabel(m_currentStep),
                           navigationStateLabel(complete, true, true));
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float fontSize = ImGui::GetFontSize();
    const float spacing = STEP_COUNT > 1
        ? availableWidth / static_cast<float>(STEP_COUNT)
        : availableWidth;
    const float circleRadius = std::min(fontSize * 0.35f, spacing * 0.3f);
    const float totalHeight = circleRadius * 2.0f +
                              (compact ? 2.0f : fontSize + 4.0f);
    const bool pinned = pinnedPreparationActive();
    const bool previewComplete = pinned &&
        m_preparationFlow.snapshot().stage(PreparationStageId::CarvePreview).state ==
            PreparationStageState::Complete;

    for (int index = 0; index < STEP_COUNT; ++index) {
        const auto step = static_cast<Step>(index);
        const auto stage = preparationStage(step);
        bool visited = index <= m_maxStepVisited;
        bool reachable = carve::canNavigateDirectCarveStep(
            index, m_maxStepVisited, STEP_COUNT);
        bool satisfied = isStepSatisfied(step);
        if (pinned && stage) {
            const auto state = m_preparationFlow.snapshot().stage(*stage).state;
            reachable = state != PreparationStageState::Locked;
            satisfied = state == PreparationStageState::Complete;
            visited = reachable;
        } else if (pinned) {
            reachable = previewComplete && reachable;
        }

        const char* label = stepLabel(step);
        const float labelWidth = ImGui::CalcTextSize(label).x;
        const float centerX = cursor.x + spacing * (static_cast<float>(index) + 0.5f);
        const float centerY = cursor.y + circleRadius;
        ImVec4 color = kDimmed;
        if (satisfied) color = kGreen;
        else if (index == currentIndex || visited || reachable) color = kYellow;
        const ImU32 packedColor = ImGui::ColorConvertFloat4ToU32(color);

        const ImVec2 hitMin{centerX - spacing * 0.5f, cursor.y};
        const ImVec2 hitMax{centerX + spacing * 0.5f, cursor.y + totalHeight};
        ImGui::SetCursorScreenPos(hitMin);
        char buttonId[32];
        std::snprintf(buttonId, sizeof(buttonId), "##step%d", index);
        if (ImGui::InvisibleButton(
                buttonId, ImVec2(hitMax.x - hitMin.x, hitMax.y - hitMin.y))) {
            navigateToStep(step);
        }
        const bool hovered = ImGui::IsItemHovered();
        const bool focused = ImGui::IsItemFocused();
        const bool highlighted = hovered || focused;
        if (hovered && reachable) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (highlighted) {
            const char* instruction = !reachable
                ? "Complete the previous stage first"
                : (satisfied ? "This stage is complete"
                             : "Complete the requirements shown in this stage");
            ImGui::SetTooltip("%s - %s. %s",
                              label,
                              navigationStateLabel(satisfied,
                                                   index == currentIndex,
                                                   reachable),
                              instruction);
        }

        if (visited || (reachable && index == m_maxStepVisited + 1))
            drawList->AddCircleFilled({centerX, centerY}, circleRadius, packedColor);
        else
            drawList->AddCircle({centerX, centerY}, circleRadius, packedColor, 0, 1.5f);
        if (highlighted)
            drawList->AddCircle({centerX, centerY}, circleRadius + 2.0f,
                                ImGui::GetColorU32(ImGuiCol_NavHighlight), 0, 2.0f);

        char markerNumber[8];
        const char* marker = navigationStateIcon(
            satisfied, index == currentIndex, reachable);
        if (compact && marker[0] == '\0') {
            std::snprintf(markerNumber, sizeof(markerNumber), "%d", index + 1);
            marker = markerNumber;
        }
        if (marker[0] != '\0') {
            const ImVec2 markerSize = ImGui::CalcTextSize(marker);
            drawList->AddText({centerX - markerSize.x * 0.5f,
                               centerY - markerSize.y * 0.5f},
                              ImGui::GetColorU32(ImGuiCol_Text),
                              marker);
        }
        if (!compact) {
            drawList->AddText({centerX - labelWidth * 0.5f,
                               centerY + circleRadius + 2.0f},
                              packedColor,
                              label);
        }

        if (index < STEP_COUNT - 1) {
            const float nextCenterX =
                cursor.x + spacing * (static_cast<float>(index) + 1.5f);
            const ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(
                satisfied ? kGreen : kDimmed);
            drawList->AddLine({centerX + circleRadius + 2.0f, centerY},
                              {nextCenterX - circleRadius - 2.0f, centerY},
                              lineColor,
                              1.5f);
        }
    }
    ImGui::SetCursorScreenPos({cursor.x, cursor.y + totalHeight + 2.0f});
    ImGui::Dummy({0, 0});
}

} // namespace dw

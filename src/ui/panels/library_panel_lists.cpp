#include "library_panel.h"

#include <algorithm>

#include <imgui.h>

#include "../../core/config/config.h"

namespace dw {

namespace {

void renderCardListBottomClearance() {
    // SetScrollHereY() cannot reveal the bottom of the final card unless the
    // child has enough scrollable content below it to honor that target. A
    // text-line clearance also keeps scaled-font descenders away from the
    // child clip edge instead of cutting the final identity line in half.
    ImGui::NewLine();
    ImGui::Dummy(ImVec2(0.0F, ImGui::GetTextLineHeightWithSpacing()));
}

} // namespace

void LibraryPanel::renderModelList() {
    ImGui::BeginChild("ModelList", ImVec2(0, 0), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (m_showThumbnails && ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0F) {
            const float maxThumb = ImGui::GetContentRegionAvail().x;
            m_thumbnailSize =
                std::clamp(m_thumbnailSize + wheel * 16.0F, THUMB_MIN, maxThumb);
            Config::instance().setLibraryThumbSize(m_thumbnailSize);
            ImGui::GetIO().MouseWheel = 0.0F;
        }
    }

    if (m_models.empty()) {
        ImGui::TextDisabled("No models in library");
        ImGui::TextDisabled("Import models using File > Import");
    } else if (m_showThumbnails) {
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        constexpr float padding = 4.0F;
        const int columns = std::max(
            1, static_cast<int>(availableWidth / (m_thumbnailSize + padding)));
        const float thumbnailSize =
            availableWidth / static_cast<float>(columns) - padding;
        int column = 0;
        for (std::size_t index = 0; index < m_models.size(); ++index) {
            renderModelItem(m_models[index], static_cast<int>(index), thumbnailSize);
            if (++column < columns)
                ImGui::SameLine(0.0F, 0.0F);
            else
                column = 0;
        }
        renderCardListBottomClearance();
    } else {
        for (std::size_t index = 0; index < m_models.size(); ++index)
            renderModelItem(m_models[index], static_cast<int>(index));
    }
    ImGui::EndChild();
}

void LibraryPanel::renderGCodeList() {
    ImGui::BeginChild("GCodeList", ImVec2(0, 0), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (m_showThumbnails && ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0F) {
            const float maxThumb = ImGui::GetContentRegionAvail().x;
            m_thumbnailSize =
                std::clamp(m_thumbnailSize + wheel * 16.0F, THUMB_MIN, maxThumb);
            Config::instance().setLibraryThumbSize(m_thumbnailSize);
            ImGui::GetIO().MouseWheel = 0.0F;
        }
    }

    if (m_gcodeFiles.empty()) {
        ImGui::TextDisabled("No G-code files in library");
        ImGui::TextDisabled("Import G-code using File > Import");
    } else if (m_showThumbnails) {
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        constexpr float padding = 4.0F;
        const int columns = std::max(
            1, static_cast<int>(availableWidth / (m_thumbnailSize + padding)));
        const float thumbnailSize =
            availableWidth / static_cast<float>(columns) - padding;
        int column = 0;
        for (std::size_t index = 0; index < m_gcodeFiles.size(); ++index) {
            renderGCodeItem(m_gcodeFiles[index], static_cast<int>(index), thumbnailSize);
            if (++column < columns)
                ImGui::SameLine(0.0F, 0.0F);
            else
                column = 0;
        }
        renderCardListBottomClearance();
    } else {
        for (std::size_t index = 0; index < m_gcodeFiles.size(); ++index)
            renderGCodeItem(m_gcodeFiles[index], static_cast<int>(index));
    }
    ImGui::EndChild();
}

void LibraryPanel::renderCombinedList() {
    ImGui::BeginChild("CombinedList", ImVec2(0, 0), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (m_showThumbnails && ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0F) {
            const float maxThumb = ImGui::GetContentRegionAvail().x;
            m_thumbnailSize =
                std::clamp(m_thumbnailSize + wheel * 16.0F, THUMB_MIN, maxThumb);
            Config::instance().setLibraryThumbSize(m_thumbnailSize);
            ImGui::GetIO().MouseWheel = 0.0F;
        }
    }

    if (m_models.empty() && m_gcodeFiles.empty()) {
        ImGui::TextDisabled("Library is empty");
        ImGui::TextDisabled("Import files using File > Import");
    } else if (m_showThumbnails) {
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        constexpr float padding = 4.0F;
        const int columns = std::max(
            1, static_cast<int>(availableWidth / (m_thumbnailSize + padding)));
        const float thumbnailSize =
            availableWidth / static_cast<float>(columns) - padding;
        int column = 0;
        for (std::size_t index = 0; index < m_models.size(); ++index) {
            renderModelItem(m_models[index], static_cast<int>(index), thumbnailSize);
            if (++column < columns)
                ImGui::SameLine(0.0F, 0.0F);
            else
                column = 0;
        }
        for (std::size_t index = 0; index < m_gcodeFiles.size(); ++index) {
            renderGCodeItem(m_gcodeFiles[index],
                            static_cast<int>(index + m_models.size()),
                            thumbnailSize);
            if (++column < columns)
                ImGui::SameLine(0.0F, 0.0F);
            else
                column = 0;
        }
        renderCardListBottomClearance();
    } else {
        for (std::size_t index = 0; index < m_models.size(); ++index)
            renderModelItem(m_models[index], static_cast<int>(index));
        for (std::size_t index = 0; index < m_gcodeFiles.size(); ++index) {
            renderGCodeItem(m_gcodeFiles[index],
                            static_cast<int>(index + m_models.size()));
        }
    }
    ImGui::EndChild();
}

} // namespace dw

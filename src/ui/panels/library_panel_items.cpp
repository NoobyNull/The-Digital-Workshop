// library_panel_items.cpp — Item rendering for LibraryPanel
// Split from library_panel.cpp to keep item rendering isolated.
// Contains item click handling and individual Model/G-code item rendering.

#include "library_panel.h"

#include <string>

#include <imgui.h>

#include "../../modules/design_library/library_card_layout.h"
#include "../context_menu_manager.h"
#include "../icons.h"
#include "../ui_colors.h"

namespace dw {

void LibraryPanel::revealPendingSelection(workshop::LibraryItemRef item) {
    if (!m_pendingSelectionReveal ||
        m_pendingSelectionReveal->kind != item.kind ||
        m_pendingSelectionReveal->item != item.item) {
        return;
    }
    // The selected card can be the final item in a short picker. Align its
    // bottom edge so every wrapped identity line is visible instead of leaving
    // the last line beneath the child-window clip rectangle.
    ImGui::SetScrollHereY(1.0F);
    m_pendingSelectionReveal.reset();
}

bool LibraryPanel::handleModelClick(const ModelRecord& model) {
    const bool previewRequested = ImGui::IsMouseDoubleClicked(0);
    if (previewRequested || isProjectModelPicker()) {
        m_selectedModelIds = {model.id};
        m_lastClickedModelId = model.id;
        m_selectedGCodeIds.clear();
    } else if (ImGui::GetIO().KeyCtrl) {
        if (m_selectedModelIds.count(model.id))
            m_selectedModelIds.erase(model.id);
        else
            m_selectedModelIds.insert(model.id);
        m_lastClickedModelId = model.id;
        m_selectedGCodeIds.clear();
    } else if (ImGui::GetIO().KeyShift && m_lastClickedModelId != -1) {
        m_selectedModelIds.clear();
        bool inRange = false;
        for (const auto& candidate : m_models) {
            if (candidate.id == model.id || candidate.id == m_lastClickedModelId)
                inRange = !inRange ? true : false;
            if (inRange || candidate.id == model.id || candidate.id == m_lastClickedModelId)
                m_selectedModelIds.insert(candidate.id);
        }
        m_selectedGCodeIds.clear();
    } else {
        m_selectedModelIds = {model.id};
        m_lastClickedModelId = model.id;
        m_selectedGCodeIds.clear();
    }
    emitSelectionChanged();
    if (previewRequested) {
        emitPreviewRequested({workshop::LibraryItemKind::Model,
                              workshop::LibraryItemId(model.id)});
    }
    return true;
}

bool LibraryPanel::handleGCodeClick(const GCodeRecord& gcode) {
    if (isProjectModelPicker())
        return false;
    const bool previewRequested = ImGui::IsMouseDoubleClicked(0);
    if (previewRequested) {
        m_selectedGCodeIds = {gcode.id};
        m_lastClickedGCodeId = gcode.id;
        m_selectedModelIds.clear();
    } else if (ImGui::GetIO().KeyCtrl) {
        if (m_selectedGCodeIds.count(gcode.id))
            m_selectedGCodeIds.erase(gcode.id);
        else
            m_selectedGCodeIds.insert(gcode.id);
        m_lastClickedGCodeId = gcode.id;
        m_selectedModelIds.clear();
    } else if (ImGui::GetIO().KeyShift && m_lastClickedGCodeId != -1) {
        m_selectedGCodeIds.clear();
        bool inRange = false;
        for (const auto& candidate : m_gcodeFiles) {
            if (candidate.id == gcode.id || candidate.id == m_lastClickedGCodeId)
                inRange = !inRange ? true : false;
            if (inRange || candidate.id == gcode.id || candidate.id == m_lastClickedGCodeId)
                m_selectedGCodeIds.insert(candidate.id);
        }
        m_selectedModelIds.clear();
    } else {
        m_selectedGCodeIds = {gcode.id};
        m_lastClickedGCodeId = gcode.id;
        m_selectedModelIds.clear();
    }
    emitSelectionChanged();
    if (previewRequested) {
        emitPreviewRequested({workshop::LibraryItemKind::GCode,
                              workshop::LibraryItemId(gcode.id)});
    }
    return true;
}

void LibraryPanel::renderModelItem(const ModelRecord& model,
                                   [[maybe_unused]] int index,
                                   float thumbOverride) {
    ImGui::PushID(static_cast<int>(model.id));

    bool isSelected = m_selectedModelIds.count(model.id) > 0;

    if (m_showThumbnails) {
        // Grid cell: thumbnail with name below, details on hover
        float ts = thumbOverride > 0.0f ? thumbOverride : m_thumbnailSize;
        float pad = 2.0f;
        constexpr float labelGap = 1.0F;
        const float wrappedNameHeight =
            ImGui::CalcTextSize(model.name.c_str(), nullptr, false, ts).y;
        const auto labelLayout = design_library::makeLibraryCardLabelLayout(
            ts,
            wrappedNameHeight,
            ImGui::GetTextLineHeight(),
            pad,
            labelGap,
            ImGui::GetStyle().ItemSpacing.y);

        if (ImGui::Selectable("##item",
                              isSelected,
                              ImGuiSelectableFlags_AllowDoubleClick |
                                  ImGuiSelectableFlags_DontClosePopups,
                              ImVec2(ts + pad * 2, labelLayout.cellHeight))) {
            (void)handleModelClick(model);
        }
        revealPendingSelection({workshop::LibraryItemKind::Model,
                                workshop::LibraryItemId(model.id)});

        // Right-click: ensure clicked item is in selection
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (!m_selectedModelIds.count(model.id)) {
                m_selectedModelIds = {model.id};
                m_lastClickedModelId = model.id;
                m_selectedGCodeIds.clear();
                emitSelectionChanged();
            }
        }

        m_currentContextMenuModel = model;
        registerContextMenuEntries();
        if (ImGui::BeginPopupContextItem("LibraryPanel_ModelContext")) {
            m_contextMenuManager->render("LibraryPanel_ModelContext");
            ImGui::EndPopup();
        }
        m_currentContextMenuModel = std::nullopt;

        // Delayed hover tooltip with details
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            std::string tip = model.name + "\n" + model.fileFormat;
            if (model.triangleCount > 0) {
                if (model.triangleCount >= 1000000)
                    tip += " | " + std::to_string(model.triangleCount / 1000000) + "M tris";
                else if (model.triangleCount >= 1000)
                    tip += " | " + std::to_string(model.triangleCount / 1000) + "K tris";
                else
                    tip += " | " + std::to_string(model.triangleCount) + " tris";
            }
            if (model.vertexCount > 0) {
                if (model.vertexCount >= 1000000)
                    tip += "\n" + std::to_string(model.vertexCount / 1000000) + "M verts";
                else if (model.vertexCount >= 1000)
                    tip += "\n" + std::to_string(model.vertexCount / 1000) + "K verts";
                else
                    tip += "\n" + std::to_string(model.vertexCount) + " verts";
            }
            if (!model.descriptorHover.empty()) {
                tip += "\n\n" + model.descriptorHover;
            }
            ImGui::SetTooltip("%s", tip.c_str());
        }

        // Draw thumbnail over selectable
        ImVec2 itemMin = ImGui::GetItemRectMin();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImVec2 thumbMin = ImVec2(itemMin.x + pad, itemMin.y + pad);
        ImVec2 thumbMax = ImVec2(thumbMin.x + ts, thumbMin.y + ts);

        GLuint tex = getThumbnailTexture(model);
        if (tex != 0) {
            drawList->AddImageRounded((ImTextureID)(intptr_t)tex,
                                      thumbMin,
                                      thumbMax,
                                      ImVec2(0, 0),
                                      ImVec2(1, 1),
                                      IM_COL32(255, 255, 255, 255),
                                      4.0f);
        } else {
            GLuint placeholder = getPlaceholderTexture();
            if (placeholder != 0) {
                drawList->AddImageRounded((ImTextureID)(intptr_t)placeholder,
                                          thumbMin,
                                          thumbMax,
                                          ImVec2(0, 0),
                                          ImVec2(1, 1),
                                          IM_COL32(255, 255, 255, 180),
                                          4.0f);
            } else {
                drawList->AddRectFilled(thumbMin, thumbMax, IM_COL32(60, 60, 60, 255), 4.0f);
                drawList->AddRect(thumbMin, thumbMax, IM_COL32(80, 80, 80, 255), 4.0f);
                const char* icon = Icons::Model;
                ImVec2 iconSize = ImGui::CalcTextSize(icon);
                ImVec2 iconPos = ImVec2(thumbMin.x + (ts - iconSize.x) * 0.5f,
                                        thumbMin.y + (ts - iconSize.y) * 0.5f);
                drawList->AddText(iconPos, IM_COL32(128, 128, 128, 255), icon);
            }
        }

        // Selection highlight
        if (isSelected) {
            drawList->AddRect(
                thumbMin, thumbMax, ImGui::GetColorU32(ImGuiCol_ButtonActive), 4.0f, 0, 2.0f);
        }
        if (isPickerProjectMember({workshop::LibraryItemKind::Model,
                                   workshop::LibraryItemId(model.id)})) {
            drawList->AddText(ImVec2(thumbMin.x + 4.0f, thumbMin.y + 4.0f),
                              ImGui::GetColorU32(colors::kSuccess),
                              "IN PROJECT");
        }

        // Name below thumbnail. Its measured wrapped height is part of the
        // selectable, so the full identity remains visible at narrow scales.
        ImVec2 namePos = ImVec2(itemMin.x + pad, thumbMax.y + labelGap);
        ImVec4 clipRect(namePos.x,
                        namePos.y,
                        namePos.x + labelLayout.wrapWidth,
                        namePos.y + labelLayout.labelHeight);
        drawList->AddText(nullptr,
                          0.0f,
                          namePos,
                          ImGui::GetColorU32(ImGuiCol_Text),
                          model.name.c_str(),
                          nullptr,
                          labelLayout.wrapWidth,
                          &clipRect);
    } else {
        // List view - compact row
        if (ImGui::Selectable("##item",
                              isSelected,
                              ImGuiSelectableFlags_AllowDoubleClick |
                                  ImGuiSelectableFlags_DontClosePopups,
                              ImVec2(0, ImGui::GetTextLineHeightWithSpacing()))) {
            (void)handleModelClick(model);
        }
        revealPendingSelection({workshop::LibraryItemKind::Model,
                                workshop::LibraryItemId(model.id)});

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (!m_selectedModelIds.count(model.id)) {
                m_selectedModelIds = {model.id};
                m_lastClickedModelId = model.id;
                m_selectedGCodeIds.clear();
                emitSelectionChanged();
            }
        }

        m_currentContextMenuModel = model;
        registerContextMenuEntries();
        if (ImGui::BeginPopupContextItem("LibraryPanel_ModelContext")) {
            m_contextMenuManager->render("LibraryPanel_ModelContext");
            ImGui::EndPopup();
        }
        m_currentContextMenuModel = std::nullopt;

        // Draw text over the selectable
        ImVec2 itemMin = ImGui::GetItemRectMin();
        ImVec2 itemMax = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float pad = 4.0f;

        // Name on the left
        std::string label = std::string(Icons::Model) + " " + model.name;
        float textMaxX = itemMax.x - 70.0f;
        ImVec4 clipRect(itemMin.x + pad, itemMin.y, textMaxX, itemMax.y);
        drawList->AddText(nullptr,
                          0.0f,
                          ImVec2(itemMin.x + pad, itemMin.y + 3.0f),
                          ImGui::GetColorU32(ImGuiCol_Text),
                          label.c_str(),
                          nullptr,
                          0.0f,
                          &clipRect);

        // Format on the right
        ImVec2 fmtSize = ImGui::CalcTextSize(model.fileFormat.c_str());
        drawList->AddText(ImVec2(itemMax.x - fmtSize.x - pad, itemMin.y + 3.0f),
                          ImGui::GetColorU32(ImGuiCol_TextDisabled),
                          model.fileFormat.c_str());
    }

    ImGui::PopID();
}

void LibraryPanel::renderGCodeItem(const GCodeRecord& gcode,
                                   [[maybe_unused]] int index,
                                   float thumbOverride) {
    ImGui::PushID(static_cast<int>(gcode.id + 1000000)); // Offset to avoid ID collision with models

    bool isSelected = m_selectedGCodeIds.count(gcode.id) > 0;

    if (m_showThumbnails) {
        // Grid cell: placeholder with name below, details on hover
        float ts = thumbOverride > 0.0f ? thumbOverride : m_thumbnailSize;
        float pad = 2.0f;
        constexpr float labelGap = 1.0F;
        const float wrappedNameHeight =
            ImGui::CalcTextSize(gcode.name.c_str(), nullptr, false, ts).y;
        const auto labelLayout = design_library::makeLibraryCardLabelLayout(
            ts,
            wrappedNameHeight,
            ImGui::GetTextLineHeight(),
            pad,
            labelGap,
            ImGui::GetStyle().ItemSpacing.y);

        if (ImGui::Selectable("##gcodeitem",
                              isSelected,
                              ImGuiSelectableFlags_AllowDoubleClick |
                                  ImGuiSelectableFlags_DontClosePopups,
                              ImVec2(ts + pad * 2, labelLayout.cellHeight))) {
            (void)handleGCodeClick(gcode);
        }
        revealPendingSelection({workshop::LibraryItemKind::GCode,
                                workshop::LibraryItemId(gcode.id)});

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (!m_selectedGCodeIds.count(gcode.id)) {
                m_selectedGCodeIds = {gcode.id};
                m_lastClickedGCodeId = gcode.id;
                m_selectedModelIds.clear();
                emitSelectionChanged();
            }
        }

        m_currentContextMenuGCode = gcode;
        registerContextMenuEntries();
        if (ImGui::BeginPopupContextItem("LibraryPanel_GCodeContext")) {
            m_contextMenuManager->render("LibraryPanel_GCodeContext");
            ImGui::EndPopup();
        }
        m_currentContextMenuGCode = std::nullopt;

        // Delayed hover tooltip with details
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            std::string tip = gcode.name + "\nG-code";
            if (gcode.estimatedTime > 0) {
                int minutes = static_cast<int>(gcode.estimatedTime);
                tip += " | " + std::to_string(minutes) + "min";
            }
            if (gcode.totalDistance > 0) {
                tip += " | " + std::to_string(static_cast<int>(gcode.totalDistance)) + "mm";
            }
            ImGui::SetTooltip("%s", tip.c_str());
        }

        // Draw placeholder
        ImVec2 itemMin = ImGui::GetItemRectMin();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImVec2 thumbMin = ImVec2(itemMin.x + pad, itemMin.y + pad);
        ImVec2 thumbMax = ImVec2(thumbMin.x + ts, thumbMin.y + ts);

        drawList->AddRectFilled(thumbMin, thumbMax, IM_COL32(50, 70, 90, 255), 4.0f);
        drawList->AddRect(thumbMin, thumbMax, IM_COL32(70, 100, 130, 255), 4.0f);

        // Center "GC" text in placeholder
        const char* icon = "GC";
        ImVec2 iconSize = ImGui::CalcTextSize(icon);
        ImVec2 iconPos = ImVec2(thumbMin.x + (ts - iconSize.x) * 0.5f,
                                thumbMin.y + (ts - iconSize.y) * 0.5f);
        drawList->AddText(iconPos, IM_COL32(150, 180, 220, 255), icon);

        // Selection highlight
        if (isSelected) {
            drawList->AddRect(
                thumbMin, thumbMax, ImGui::GetColorU32(ImGuiCol_ButtonActive), 4.0f, 0, 2.0f);
        }
        if (isPickerProjectMember({workshop::LibraryItemKind::GCode,
                                   workshop::LibraryItemId(gcode.id)})) {
            drawList->AddText(ImVec2(thumbMin.x + 4.0f, thumbMin.y + 4.0f),
                              ImGui::GetColorU32(colors::kSuccess),
                              "IN PROJECT");
        }

        // Reserve every measured wrapped line for G-code identities as well.
        ImVec2 namePos = ImVec2(itemMin.x + pad, thumbMax.y + labelGap);
        ImVec4 clipRect(namePos.x,
                        namePos.y,
                        namePos.x + labelLayout.wrapWidth,
                        namePos.y + labelLayout.labelHeight);
        drawList->AddText(nullptr,
                          0.0f,
                          namePos,
                          ImGui::GetColorU32(ImGuiCol_Text),
                          gcode.name.c_str(),
                          nullptr,
                          labelLayout.wrapWidth,
                          &clipRect);
    } else {
        // List view - compact row
        if (ImGui::Selectable("##gcodeitem",
                              isSelected,
                              ImGuiSelectableFlags_AllowDoubleClick |
                                  ImGuiSelectableFlags_DontClosePopups,
                              ImVec2(0, ImGui::GetTextLineHeightWithSpacing()))) {
            (void)handleGCodeClick(gcode);
        }
        revealPendingSelection({workshop::LibraryItemKind::GCode,
                                workshop::LibraryItemId(gcode.id)});

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (!m_selectedGCodeIds.count(gcode.id)) {
                m_selectedGCodeIds = {gcode.id};
                m_lastClickedGCodeId = gcode.id;
                m_selectedModelIds.clear();
                emitSelectionChanged();
            }
        }

        m_currentContextMenuGCode = gcode;
        registerContextMenuEntries();
        if (ImGui::BeginPopupContextItem("LibraryPanel_GCodeContext")) {
            m_contextMenuManager->render("LibraryPanel_GCodeContext");
            ImGui::EndPopup();
        }
        m_currentContextMenuGCode = std::nullopt;

        // Draw text
        ImVec2 itemMin = ImGui::GetItemRectMin();
        ImVec2 itemMax = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float pad = 4.0f;

        // Name on the left with icon
        std::string label = "GC " + gcode.name;
        float textMaxX = itemMax.x - 70.0f;
        ImVec4 clipRect(itemMin.x + pad, itemMin.y, textMaxX, itemMax.y);
        drawList->AddText(nullptr,
                          0.0f,
                          ImVec2(itemMin.x + pad, itemMin.y + 3.0f),
                          ImGui::GetColorU32(ImGuiCol_Text),
                          label.c_str(),
                          nullptr,
                          0.0f,
                          &clipRect);

        // Format on the right
        const char* format = "G-code";
        ImVec2 fmtSize = ImGui::CalcTextSize(format);
        drawList->AddText(ImVec2(itemMax.x - fmtSize.x - pad, itemMin.y + 3.0f),
                          ImGui::GetColorU32(ImGuiCol_TextDisabled),
                          format);
    }

    ImGui::PopID();
}

} // namespace dw

// library_panel_items.cpp — Item rendering for LibraryPanel
// Split from library_panel.cpp to stay within the 800-line .cpp limit.
// Contains: renderModelList, renderModelItem, renderGCodeList, renderGCodeItem,
//           renderCombinedList, registerContextMenuEntries

#include "library_panel.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>

#include "../../core/config/config.h"
#include "../../core/paths/path_resolver.h"
#include "../../core/project/project.h"
#include "../../core/utils/file_utils.h"
#include "../context_menu_manager.h"
#include "../icons.h"

namespace dw {

void LibraryPanel::renderModelList() {
    ImGui::BeginChild("ModelList", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // Ctrl+scroll to zoom thumbnails in grid view
    if (m_showThumbnails && ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float maxThumb = ImGui::GetContentRegionAvail().x;
            m_thumbnailSize = std::clamp(m_thumbnailSize + wheel * 16.0f, THUMB_MIN, maxThumb);
            Config::instance().setLibraryThumbSize(m_thumbnailSize);
            ImGui::GetIO().MouseWheel = 0.0f; // consume so child doesn't scroll
        }
    }

    if (m_models.empty()) {
        ImGui::TextDisabled("No models in library");
        ImGui::TextDisabled("Import models using File > Import");
    } else if (m_showThumbnails) {
        // True grid layout with spill
        float availW = ImGui::GetContentRegionAvail().x;
        float pad = 4.0f;
        float cellBase = m_thumbnailSize + pad;
        int columns = std::max(1, static_cast<int>(availW / cellBase));
        // Spill: distribute leftover space evenly into each cell
        float cellW = availW / static_cast<float>(columns);
        float thumbSize = cellW - pad;

        int col = 0;
        for (size_t i = 0; i < m_models.size(); ++i) {
            renderModelItem(m_models[i], static_cast<int>(i), thumbSize);
            ++col;
            if (col < columns) {
                ImGui::SameLine(0.0f, 0.0f);
            } else {
                col = 0;
            }
        }
    } else {
        for (size_t i = 0; i < m_models.size(); ++i) {
            renderModelItem(m_models[i], static_cast<int>(i));
        }
    }

    ImGui::EndChild();
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
        float nameH = ImGui::GetTextLineHeightWithSpacing();
        float cellH = ts + nameH + pad * 2;

        if (ImGui::Selectable("##item",
                              isSelected,
                              ImGuiSelectableFlags_AllowDoubleClick |
                                  ImGuiSelectableFlags_DontClosePopups,
                              ImVec2(ts + pad * 2, cellH))) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                if (m_onModelOpened)
                    m_onModelOpened(model.id);
            } else if (ImGui::GetIO().KeyCtrl) {
                // Ctrl+click: toggle this item
                if (m_selectedModelIds.count(model.id))
                    m_selectedModelIds.erase(model.id);
                else
                    m_selectedModelIds.insert(model.id);
                m_lastClickedModelId = model.id;
                m_selectedGCodeIds.clear();
            } else if (ImGui::GetIO().KeyShift && m_lastClickedModelId != -1) {
                // Shift+click: range select by visible order
                m_selectedModelIds.clear();
                bool inRange = false;
                for (auto& m : m_models) {
                    if (m.id == model.id || m.id == m_lastClickedModelId)
                        inRange = !inRange ? true : false;
                    if (inRange || m.id == model.id || m.id == m_lastClickedModelId)
                        m_selectedModelIds.insert(m.id);
                }
                m_selectedGCodeIds.clear();
            } else {
                m_selectedModelIds = {model.id};
                m_lastClickedModelId = model.id;
                m_selectedGCodeIds.clear();
            }
            if (m_onModelSelected)
                m_onModelSelected(model.id);
        }

        // Right-click: ensure clicked item is in selection
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (!m_selectedModelIds.count(model.id)) {
                m_selectedModelIds = {model.id};
                m_lastClickedModelId = model.id;
                m_selectedGCodeIds.clear();
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

        // Name below thumbnail (clipped to cell width)
        ImVec2 namePos = ImVec2(itemMin.x + pad, thumbMax.y + 1.0f);
        ImVec4 clipRect(namePos.x, namePos.y, namePos.x + ts, namePos.y + nameH);
        drawList->AddText(nullptr,
                          0.0f,
                          namePos,
                          ImGui::GetColorU32(ImGuiCol_Text),
                          model.name.c_str(),
                          nullptr,
                          ts,
                          &clipRect);
    } else {
        // List view - compact row
        if (ImGui::Selectable("##item",
                              isSelected,
                              ImGuiSelectableFlags_AllowDoubleClick |
                                  ImGuiSelectableFlags_DontClosePopups,
                              ImVec2(0, ImGui::GetTextLineHeightWithSpacing()))) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                if (m_onModelOpened)
                    m_onModelOpened(model.id);
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
                for (auto& m : m_models) {
                    if (m.id == model.id || m.id == m_lastClickedModelId)
                        inRange = !inRange ? true : false;
                    if (inRange || m.id == model.id || m.id == m_lastClickedModelId)
                        m_selectedModelIds.insert(m.id);
                }
                m_selectedGCodeIds.clear();
            } else {
                m_selectedModelIds = {model.id};
                m_lastClickedModelId = model.id;
                m_selectedGCodeIds.clear();
            }
            if (m_onModelSelected)
                m_onModelSelected(model.id);
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (!m_selectedModelIds.count(model.id)) {
                m_selectedModelIds = {model.id};
                m_lastClickedModelId = model.id;
                m_selectedGCodeIds.clear();
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

void LibraryPanel::registerContextMenuEntries() {
    if (!m_contextMenuManager) {
        return;
    }

    // Model context menu -- labels reflect current selection count
    size_t modelCount = m_selectedModelIds.size();
    bool multi = modelCount > 1;
    std::string countSuffix = multi ? " (" + std::to_string(modelCount) + ")" : "";

    std::vector<ContextMenuEntry> modelEntries = {
        {"Open",
         [this]() {
             if (m_onModelOpened && m_currentContextMenuModel) {
                 m_onModelOpened(m_currentContextMenuModel->id);
             }
         }},
        {"Regenerate Thumbnail" + (multi ? "s" + countSuffix : ""),
         [this]() {
             if (!m_onRegenerateThumbnail)
                 return;
             std::vector<int64_t> ids(m_selectedModelIds.begin(), m_selectedModelIds.end());
             m_onRegenerateThumbnail(ids);
         }},
        {
            "Assign Default Material" + countSuffix,
            [this]() {
                if (!m_onAssignDefaultMaterial)
                    return;
                for (int64_t id : m_selectedModelIds) {
                    m_onAssignDefaultMaterial(id);
                }
            },
            "", // icon
            [this]() {
                return Config::instance().getDefaultMaterialId() > 0;
            } // enabled
        },
        {"Assign Category" + countSuffix, [this]() { m_showCategoryAssignDialog = true; }},
        {"AI Retag" + countSuffix,
         [this]() {
             if (m_onTagImage && !m_selectedModelIds.empty()) {
                 std::vector<int64_t> ids(m_selectedModelIds.begin(), m_selectedModelIds.end());
                 m_onTagImage(ids);
             }
         }},
        {"Add to Project" + countSuffix,
         [this]() {
             if (!m_projectManager || !m_projectManager->currentProject())
                 return;
             for (int64_t id : m_selectedModelIds) {
                 if (!m_projectManager->currentProject()->hasModel(id)) {
                     m_projectManager->addModelToProject(id);
                 }
             }
         },
         "",
         [this]() {
             return m_projectManager != nullptr && m_projectManager->currentProject() != nullptr;
         }},
        ContextMenuEntry::separator(),
        {"Rename",
         [this]() {
             if (m_currentContextMenuModel) {
                 m_showRenameDialog = true;
                 m_renameModelId = m_currentContextMenuModel->id;
                 std::strncpy(m_renameBuffer,
                              m_currentContextMenuModel->name.c_str(),
                              sizeof(m_renameBuffer) - 1);
                 m_renameBuffer[sizeof(m_renameBuffer) - 1] = '\0';
             }
         },
         "",                                                   // icon
         [this]() { return m_selectedModelIds.size() == 1; }}, // only for single selection
        {"Delete" + countSuffix,
         [this]() {
             m_showDeleteConfirm = true;
             m_deleteIsGCode = false;
             m_deleteItemIds.assign(m_selectedModelIds.begin(), m_selectedModelIds.end());
             if (m_deleteItemIds.size() == 1 && m_currentContextMenuModel) {
                 m_deleteItemName = m_currentContextMenuModel->name;
             } else {
                 m_deleteItemName = std::to_string(m_deleteItemIds.size()) + " items";
             }
         }},
        ContextMenuEntry::separator(),
        {"Show in Explorer",
         [this]() {
             if (m_currentContextMenuModel) {
                 auto parentDir = m_currentContextMenuModel->filePath.parent_path();
                 if (!parentDir.empty()) {
                     file::openInFileManager(parentDir);
                 }
             }
         }},
        {"Copy Path",
         [this]() {
             if (m_selectedModelIds.size() > 1) {
                 // Copy newline-separated paths for multi-select
                 std::string paths;
                 for (int64_t id : m_selectedModelIds) {
                     for (auto& m : m_models) {
                         if (m.id == id) {
                             if (!paths.empty())
                                 paths += "\n";
                             paths += PathResolver::resolve(m.filePath, PathCategory::Support).string();
                             break;
                         }
                     }
                 }
                 ImGui::SetClipboardText(paths.c_str());
             } else if (m_currentContextMenuModel) {
                 ImGui::SetClipboardText(
                     PathResolver::resolve(m_currentContextMenuModel->filePath, PathCategory::Support)
                         .string().c_str());
             }
         }},
    };

    // GCode context menu
    size_t gcodeCount = m_selectedGCodeIds.size();
    bool gcodeMulti = gcodeCount > 1;
    std::string gcodeCountSuffix = gcodeMulti ? " (" + std::to_string(gcodeCount) + ")" : "";

    std::vector<ContextMenuEntry> gcodeEntries = {
        {"Open",
         [this]() {
             if (m_onGCodeOpened && m_currentContextMenuGCode) {
                 m_onGCodeOpened(m_currentContextMenuGCode->id);
             }
         }},
        {"Add to Project" + gcodeCountSuffix,
         [this]() {
             if (!m_onGCodeAddToProject)
                 return;
             std::vector<int64_t> ids(m_selectedGCodeIds.begin(), m_selectedGCodeIds.end());
             m_onGCodeAddToProject(ids);
         },
         "",
         [this]() {
             return m_projectManager != nullptr && m_projectManager->currentProject() != nullptr;
         }},
        ContextMenuEntry::separator(),
        {"Delete" + gcodeCountSuffix,
         [this]() {
             m_showDeleteConfirm = true;
             m_deleteIsGCode = true;
             m_deleteItemIds.assign(m_selectedGCodeIds.begin(), m_selectedGCodeIds.end());
             if (m_deleteItemIds.size() == 1 && m_currentContextMenuGCode) {
                 m_deleteItemName = m_currentContextMenuGCode->name;
             } else {
                 m_deleteItemName = std::to_string(m_deleteItemIds.size()) + " items";
             }
         }},
        ContextMenuEntry::separator(),
        {"Show in Explorer",
         [this]() {
             if (m_currentContextMenuGCode) {
                 auto parentDir = m_currentContextMenuGCode->filePath.parent_path();
                 if (!parentDir.empty()) {
                     file::openInFileManager(parentDir);
                 }
             }
         }},
    };

    m_contextMenuManager->registerEntries("LibraryPanel_ModelContext", modelEntries);
    m_contextMenuManager->registerEntries("LibraryPanel_GCodeContext", gcodeEntries);
}

void LibraryPanel::renderGCodeList() {
    ImGui::BeginChild("GCodeList", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // Ctrl+scroll to zoom thumbnails in grid view
    if (m_showThumbnails && ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float maxThumb = ImGui::GetContentRegionAvail().x;
            m_thumbnailSize = std::clamp(m_thumbnailSize + wheel * 16.0f, THUMB_MIN, maxThumb);
            Config::instance().setLibraryThumbSize(m_thumbnailSize);
            ImGui::GetIO().MouseWheel = 0.0f; // consume so child doesn't scroll
        }
    }

    if (m_gcodeFiles.empty()) {
        ImGui::TextDisabled("No G-code files in library");
        ImGui::TextDisabled("Import G-code using File > Import");
    } else if (m_showThumbnails) {
        float availW = ImGui::GetContentRegionAvail().x;
        float pad = 4.0f;
        float cellBase = m_thumbnailSize + pad;
        int columns = std::max(1, static_cast<int>(availW / cellBase));
        float cellW = availW / static_cast<float>(columns);
        float thumbSize = cellW - pad;

        int col = 0;
        for (size_t i = 0; i < m_gcodeFiles.size(); ++i) {
            renderGCodeItem(m_gcodeFiles[i], static_cast<int>(i), thumbSize);
            ++col;
            if (col < columns) {
                ImGui::SameLine(0.0f, 0.0f);
            } else {
                col = 0;
            }
        }
    } else {
        for (size_t i = 0; i < m_gcodeFiles.size(); ++i) {
            renderGCodeItem(m_gcodeFiles[i], static_cast<int>(i));
        }
    }

    ImGui::EndChild();
}

void LibraryPanel::renderCombinedList() {
    ImGui::BeginChild(
        "CombinedList", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // Ctrl+scroll to zoom thumbnails in grid view
    if (m_showThumbnails && ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float maxThumb = ImGui::GetContentRegionAvail().x;
            m_thumbnailSize = std::clamp(m_thumbnailSize + wheel * 16.0f, THUMB_MIN, maxThumb);
            Config::instance().setLibraryThumbSize(m_thumbnailSize);
            ImGui::GetIO().MouseWheel = 0.0f; // consume so child doesn't scroll
        }
    }

    if (m_models.empty() && m_gcodeFiles.empty()) {
        ImGui::TextDisabled("Library is empty");
        ImGui::TextDisabled("Import files using File > Import");
    } else if (m_showThumbnails) {
        float availW = ImGui::GetContentRegionAvail().x;
        float pad = 4.0f;
        float cellBase = m_thumbnailSize + pad;
        int columns = std::max(1, static_cast<int>(availW / cellBase));
        float cellW = availW / static_cast<float>(columns);
        float thumbSize = cellW - pad;

        int col = 0;
        for (size_t i = 0; i < m_models.size(); ++i) {
            renderModelItem(m_models[i], static_cast<int>(i), thumbSize);
            ++col;
            if (col < columns) {
                ImGui::SameLine(0.0f, 0.0f);
            } else {
                col = 0;
            }
        }
        for (size_t i = 0; i < m_gcodeFiles.size(); ++i) {
            renderGCodeItem(m_gcodeFiles[i], static_cast<int>(i + m_models.size()), thumbSize);
            ++col;
            if (col < columns) {
                ImGui::SameLine(0.0f, 0.0f);
            } else {
                col = 0;
            }
        }
    } else {
        for (size_t i = 0; i < m_models.size(); ++i) {
            renderModelItem(m_models[i], static_cast<int>(i));
        }
        for (size_t i = 0; i < m_gcodeFiles.size(); ++i) {
            renderGCodeItem(m_gcodeFiles[i], static_cast<int>(i + m_models.size()));
        }
    }

    ImGui::EndChild();
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
        float nameH = ImGui::GetTextLineHeightWithSpacing();
        float cellH = ts + nameH + pad * 2;

        if (ImGui::Selectable("##gcodeitem",
                              isSelected,
                              ImGuiSelectableFlags_AllowDoubleClick |
                                  ImGuiSelectableFlags_DontClosePopups,
                              ImVec2(ts + pad * 2, cellH))) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                if (m_onGCodeOpened)
                    m_onGCodeOpened(gcode.id);
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
                for (auto& g : m_gcodeFiles) {
                    if (g.id == gcode.id || g.id == m_lastClickedGCodeId)
                        inRange = !inRange ? true : false;
                    if (inRange || g.id == gcode.id || g.id == m_lastClickedGCodeId)
                        m_selectedGCodeIds.insert(g.id);
                }
                m_selectedModelIds.clear();
            } else {
                m_selectedGCodeIds = {gcode.id};
                m_lastClickedGCodeId = gcode.id;
                m_selectedModelIds.clear();
            }
            if (m_onGCodeSelected)
                m_onGCodeSelected(gcode.id);
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (!m_selectedGCodeIds.count(gcode.id)) {
                m_selectedGCodeIds = {gcode.id};
                m_lastClickedGCodeId = gcode.id;
                m_selectedModelIds.clear();
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

        // Name below placeholder (clipped)
        ImVec2 namePos = ImVec2(itemMin.x + pad, thumbMax.y + 1.0f);
        ImVec4 clipRect(namePos.x, namePos.y, namePos.x + ts, namePos.y + nameH);
        drawList->AddText(nullptr,
                          0.0f,
                          namePos,
                          ImGui::GetColorU32(ImGuiCol_Text),
                          gcode.name.c_str(),
                          nullptr,
                          ts,
                          &clipRect);
    } else {
        // List view - compact row
        if (ImGui::Selectable("##gcodeitem",
                              isSelected,
                              ImGuiSelectableFlags_AllowDoubleClick |
                                  ImGuiSelectableFlags_DontClosePopups,
                              ImVec2(0, ImGui::GetTextLineHeightWithSpacing()))) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                if (m_onGCodeOpened)
                    m_onGCodeOpened(gcode.id);
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
                for (auto& g : m_gcodeFiles) {
                    if (g.id == gcode.id || g.id == m_lastClickedGCodeId)
                        inRange = !inRange ? true : false;
                    if (inRange || g.id == gcode.id || g.id == m_lastClickedGCodeId)
                        m_selectedGCodeIds.insert(g.id);
                }
                m_selectedModelIds.clear();
            } else {
                m_selectedGCodeIds = {gcode.id};
                m_lastClickedGCodeId = gcode.id;
                m_selectedModelIds.clear();
            }
            if (m_onGCodeSelected)
                m_onGCodeSelected(gcode.id);
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (!m_selectedGCodeIds.count(gcode.id)) {
                m_selectedGCodeIds = {gcode.id};
                m_lastClickedGCodeId = gcode.id;
                m_selectedModelIds.clear();
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

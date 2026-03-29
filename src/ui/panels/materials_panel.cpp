#include "materials_panel.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <imgui.h>

#include "../../core/config/config.h"
#include "../../core/loaders/texture_loader.h"
#include "../../core/paths/path_resolver.h"
#include "../../core/materials/material_archive.h"
#include "../../core/utils/log.h"
#include "../context_menu_manager.h"
#include "../icons.h"
#include "../widgets/edit_buffer.h"

namespace dw {

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

MaterialsPanel::MaterialsPanel(MaterialManager* materialManager)
    : Panel("Materials"), m_materialManager(materialManager) {
    m_thumbnailSize = Config::instance().getMaterialsThumbSize();
    refresh();
}

MaterialsPanel::~MaterialsPanel() {
    clearTextureCache();
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void MaterialsPanel::refresh() {
    if (!m_materialManager) {
        return;
    }
    m_allMaterials = m_materialManager->getAllMaterials();
}

void MaterialsPanel::refreshStockSizes() {
    if (!m_materialManager || m_detailMaterialId < 0) {
        m_stockSizes.clear();
        return;
    }
    m_stockSizes = m_materialManager->getStockSizes(m_detailMaterialId);
}

void MaterialsPanel::selectMaterial(i64 materialId) {
    // Refresh to ensure we have latest data
    refresh();
    m_selectedMaterialId = materialId;

    // Notify selection callback
    if (m_onMaterialSelected) {
        m_onMaterialSelected(materialId);
    }
}

void MaterialsPanel::render() {
    if (!m_open) {
        return;
    }

    // Lazy initialize context menu manager on first render
    if (m_contextMenuManager == nullptr) {
        // This will be set by UIManager after panel initialization
        // For now, we create a placeholder that will be replaced
        // registerContextMenuEntries will be called once manager is available
    }

    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.4f, viewport->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver);
    applyMinSize(20, 10);
    if (ImGui::Begin(m_title.c_str(), &m_open)) {
        renderToolbar();
        ImGui::Separator();

        if (m_showDetailView) {
            // Split: grid on top, detail view on bottom
            float availH = ImGui::GetContentRegionAvail().y;
            // Calculate detail region: header + separator + table rows, minimum reasonable
            float lineH = ImGui::GetTextLineHeightWithSpacing();
            float detailMinH = lineH * 3.0f; // header + separator + at least one row
            float stockRowCount = static_cast<float>(m_stockSizes.size());
            float detailContentH = lineH * (4.0f + stockRowCount); // header, separator, add btn, rows
            float detailH = std::clamp(detailContentH, detailMinH, availH * 0.5f);
            float gridH = availH - detailH;

            ImGui::BeginChild("GridRegion", ImVec2(0, gridH), false);
            renderCategoryTabs();
            ImGui::EndChild();

            ImGui::Separator();
            ImGui::BeginChild("DetailRegion", ImVec2(0, 0), false);
            renderDetailView();
            ImGui::EndChild();
        } else {
            renderCategoryTabs();
        }

        renderAddDialog();
        renderEditForm();
        renderDeleteConfirm();
        renderStockAddEditPopup();
        renderStockDeleteConfirm();
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

void MaterialsPanel::renderToolbar() {
    // Import button (not yet implemented)
    ImGui::BeginDisabled(true);
    ImGui::Button(Icons::Import);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Coming soon");
    }

    ImGui::SameLine();

    // Export button (not yet implemented)
    bool hasSelection = (m_selectedMaterialId != -1);
    ImGui::BeginDisabled(true);
    ImGui::Button(Icons::Export);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Coming soon");
    }

    ImGui::SameLine();

    // Add new material button
    if (ImGui::Button(Icons::Add)) {
        clearBuffer(m_generatePrompt);
        m_showAddDialog = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add new material");
    }

    ImGui::SameLine();

    // Delete selected material button
    if (!hasSelection) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(Icons::Delete)) {
        if (hasSelection) {
            // Find name for confirmation dialog
            for (const auto& mat : m_allMaterials) {
                if (mat.id == m_selectedMaterialId) {
                    m_deleteName = mat.name;
                    break;
                }
            }
            m_deleteId = m_selectedMaterialId;
            m_showDeleteConfirm = true;
        }
    }
    if (!hasSelection) {
        ImGui::EndDisabled();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(hasSelection ? "Delete selected material"
                                       : "Select a material to delete");
    }

    ImGui::SameLine();

    // Assign to model button (only active when material selected AND model loaded)
    bool canAssign = hasSelection && m_modelLoaded;
    if (!canAssign) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(Icons::Assign)) {
        if (canAssign && m_onMaterialAssigned) {
            m_onMaterialAssigned(m_selectedMaterialId);
        }
    }
    if (!canAssign) {
        ImGui::EndDisabled();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (!hasSelection) {
            ImGui::SetTooltip("Select a material first");
        } else if (!m_modelLoaded) {
            ImGui::SetTooltip("Load a model into the viewport first");
        } else {
            ImGui::SetTooltip("Assign selected material to loaded model");
        }
    }

    ImGui::SameLine();

    // Refresh button
    if (ImGui::Button(Icons::Refresh)) {
        refresh();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Refresh materials list");
    }

    ImGui::SameLine();

    // Thumbnail size slider
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.1f);
    ImGui::SliderFloat("##ThumbSize", &m_thumbnailSize, THUMB_MIN, 256.0f, "%.0f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        Config::instance().setMaterialsThumbSize(m_thumbnailSize);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Thumbnail size (Ctrl+scroll in grid)");
    }

    ImGui::SameLine();

    // Search filter — takes remaining width
    float availW = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(availW > 50.0f ? availW : 50.0f);
    if (ImGui::InputTextWithHint(
            "##MatSearch",
            "Search...",
            m_searchQuery.data(),
            256,
            ImGuiInputTextFlags_CallbackResize,
            [](ImGuiInputTextCallbackData* data) -> int {
                if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                    auto* str = static_cast<std::string*>(data->UserData);
                    str->resize(data->BufTextLen);
                    data->Buf = str->data();
                }
                return 0;
            },
            &m_searchQuery)) {
        // Filtering happens in renderCategoryTabs via m_searchQuery
    }
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

void MaterialsPanel::registerContextMenuEntries() {
    if (!m_contextMenuManager) {
        return;
    }

    std::vector<ContextMenuEntry> materialEntries = {
        {"Edit",
         [this]() {
             if (m_currentContextMenuMaterial) {
                 m_editBuffer = *m_currentContextMenuMaterial;
                 m_isNewMaterial = false;
                 m_showEditForm = true;
             }
         }},
        {"Export",
         [this]() {
             if (m_currentContextMenuMaterial) {
                 log::info("MaterialsPanel",
                           "Export material requested (file dialog not yet wired)");
             }
         },
         {},        // icon
         []() { return false; }},  // disabled — coming soon
        ContextMenuEntry::separator(),
        {"Set as Default Material",
         [this]() {
             if (m_currentContextMenuMaterial) {
                 Config::instance().setDefaultMaterialId(m_currentContextMenuMaterial->id);
                 Config::instance().save();
             }
         }},
        ContextMenuEntry::separator(),
        {"Hide",
         [this]() {
             if (m_currentContextMenuMaterial && m_materialManager) {
                 m_materialManager->setMaterialHidden(m_currentContextMenuMaterial->id, true);
                 refresh();
             }
         },
         {},  // icon
         []() { return true; },  // enabled
         [this]() {
             return m_currentContextMenuMaterial && m_currentContextMenuMaterial->isBundled &&
                    !m_currentContextMenuMaterial->isHidden;
         }},
        {"Delete",
         [this]() {
             if (m_currentContextMenuMaterial) {
                 m_deleteId = m_currentContextMenuMaterial->id;
                 m_deleteName = m_currentContextMenuMaterial->name;
                 m_showDeleteConfirm = true;
             }
         },
         {},  // icon
         []() { return true; },  // enabled
         [this]() {
             return m_currentContextMenuMaterial && !m_currentContextMenuMaterial->isBundled;
         }},
    };
    m_contextMenuManager->registerEntries("MaterialsPanel_MaterialContext", materialEntries);
}

// ---------------------------------------------------------------------------
// Category tabs
// ---------------------------------------------------------------------------

void MaterialsPanel::renderCategoryTabs() {
    if (ImGui::BeginTabBar("MaterialCategories", ImGuiTabBarFlags_FittingPolicyScroll)) {
        auto makeTab = [&](const char* label,
                           const char* tooltip,
                           CategoryTab tab,
                           std::vector<MaterialRecord> materials) {
            bool selected = ImGui::BeginTabItem(label);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tooltip);
            }
            if (selected) {
                m_activeCategory = tab;
                renderMaterialGrid(materials);
                ImGui::EndTabItem();
            }
        };

        // Pre-filter all materials by search query
        std::vector<MaterialRecord> filtered;
        filtered.reserve(m_allMaterials.size());
        for (const auto& mat : m_allMaterials) {
            if (m_searchQuery.empty()) {
                filtered.push_back(mat);
            } else {
                // Case-insensitive substring match
                std::string nameLower = mat.name;
                std::string queryLower = m_searchQuery;
                for (auto& c : nameLower) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                for (auto& c : queryLower) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (nameLower.find(queryLower) != std::string::npos) {
                    filtered.push_back(mat);
                }
            }
        }

        // Build per-category lists
        auto byCategory = [&](MaterialCategory cat) {
            std::vector<MaterialRecord> result;
            for (const auto& mat : filtered) {
                if (mat.category == cat) {
                    result.push_back(mat);
                }
            }
            return result;
        };

        makeTab("All", "All materials", CategoryTab::All, filtered);
        makeTab("HW", "Hardwood", CategoryTab::Hardwood, byCategory(MaterialCategory::Hardwood));
        makeTab("SW", "Softwood", CategoryTab::Softwood, byCategory(MaterialCategory::Softwood));
        makeTab("Dom", "Domestic", CategoryTab::Domestic, byCategory(MaterialCategory::Domestic));
        makeTab(
            "Cmp", "Composite", CategoryTab::Composite, byCategory(MaterialCategory::Composite));

        ImGui::EndTabBar();
    }
}

// ---------------------------------------------------------------------------
// Material grid
// ---------------------------------------------------------------------------

void MaterialsPanel::renderMaterialGrid(const std::vector<MaterialRecord>& materials) {
    ImGui::BeginChild("MatGrid", ImVec2(0, 0), false);

    // Ctrl+scroll to resize thumbnails
    if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f) {
        m_thumbnailSize += ImGui::GetIO().MouseWheel * 16.0f;
        float maxSize = ImGui::GetContentRegionAvail().x;
        m_thumbnailSize = std::clamp(m_thumbnailSize, THUMB_MIN, std::max(THUMB_MIN, maxSize));
        Config::instance().setMaterialsThumbSize(m_thumbnailSize);
    }

    if (materials.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No materials found.");
        if (!m_searchQuery.empty()) {
            ImGui::TextDisabled("Try clearing the search filter.");
        }
        ImGui::EndChild();
        return;
    }

    const auto& style = ImGui::GetStyle();
    const float spacing = style.ItemSpacing.x;
    const float cellSize = m_thumbnailSize + spacing;
    float availW = ImGui::GetContentRegionAvail().x;
    int columns = static_cast<int>((availW + spacing) / cellSize);
    if (columns < 1) {
        columns = 1;
    }

    int col = 0;
    for (const auto& mat : materials) {
        ImGui::PushID(static_cast<int>(mat.id));

        bool isSelected = (mat.id == m_selectedMaterialId);

        // Group for each material cell
        ImGui::BeginGroup();

        // Invisible selectable underneath the cell (allows click + double-click detection)
        float pad = style.FramePadding.y * 2.0f;
        float cellH = m_thumbnailSize + ImGui::GetTextLineHeightWithSpacing() + pad;
        bool clicked = ImGui::Selectable("##mat",
                                         isSelected,
                                         ImGuiSelectableFlags_AllowDoubleClick,
                                         ImVec2(m_thumbnailSize, cellH));

        if (clicked) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                if (m_onMaterialAssigned) {
                    m_onMaterialAssigned(mat.id);
                }
            } else {
                // Single click: select and open detail view
                m_selectedMaterialId = mat.id;
                m_showDetailView = true;
                m_detailMaterialId = mat.id;
                refreshStockSizes();
                if (m_onMaterialSelected) {
                    m_onMaterialSelected(mat.id);
                }
            }
        }

        // Context menu
        m_currentContextMenuMaterial = mat;
        if (ImGui::BeginPopupContextItem("MaterialsPanel_MaterialContext")) {
            if (m_contextMenuManager) {
                m_contextMenuManager->render("MaterialsPanel_MaterialContext");
            }
            ImGui::EndPopup();
        }
        m_currentContextMenuMaterial = std::nullopt;

        // Draw thumbnail or placeholder over the selectable
        ImVec2 itemMin = ImGui::GetItemRectMin();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        float inset = style.FramePadding.x;
        ImVec2 thumbMin = ImVec2(itemMin.x + inset, itemMin.y + inset);
        ImVec2 thumbMax = ImVec2(thumbMin.x + m_thumbnailSize - inset * 2.0f,
                                 thumbMin.y + m_thumbnailSize - inset * 2.0f);
        float thumbW = thumbMax.x - thumbMin.x;
        float thumbH = thumbMax.y - thumbMin.y;

        GLuint tex = getThumbnailTexture(mat);
        if (tex != 0) {
            drawList->AddImageRounded((ImTextureID)(intptr_t)tex,
                                      thumbMin,
                                      thumbMax,
                                      ImVec2(0, 0),
                                      ImVec2(1, 1),
                                      IM_COL32(255, 255, 255, 255),
                                      style.FrameRounding);
        } else {
            // Colored placeholder with category initial
            ImU32 bgColor = categoryPlaceholderColor(mat.category);
            drawList->AddRectFilled(thumbMin, thumbMax, bgColor, style.FrameRounding);
            drawList->AddRect(thumbMin, thumbMax, IM_COL32(80, 80, 80, 200), style.FrameRounding);

            const char* initial = categoryInitial(mat.category);
            ImVec2 textSize = ImGui::CalcTextSize(initial);
            ImVec2 textPos = ImVec2(thumbMin.x + (thumbW - textSize.x) * 0.5f,
                                    thumbMin.y + (thumbH - textSize.y) * 0.5f);
            drawList->AddText(textPos, IM_COL32(220, 220, 220, 255), initial);
        }

        // Selection highlight border
        if (isSelected) {
            drawList->AddRect(
                thumbMin, thumbMax, ImGui::GetColorU32(ImGuiCol_ButtonActive), style.FrameRounding, 0, 2.0f);
        }

        // Material name below thumbnail (truncated)
        ImVec2 textPos = ImVec2(itemMin.x + inset, itemMin.y + m_thumbnailSize + style.ItemInnerSpacing.y);
        float maxNameW = m_thumbnailSize - inset * 2.0f;
        const char* nameStart = mat.name.c_str();
        ImVec4 clipRect(
            textPos.x, textPos.y, textPos.x + maxNameW, textPos.y + ImGui::GetTextLineHeight());
        drawList->AddText(nullptr,
                          0.0f,
                          textPos,
                          ImGui::GetColorU32(ImGuiCol_Text),
                          nameStart,
                          nullptr,
                          maxNameW,
                          &clipRect);

        ImGui::EndGroup();

        // Grid layout: SameLine unless we've filled a row
        ++col;
        if (col < columns) {
            ImGui::SameLine(0.0f, spacing);
        } else {
            col = 0;
        }

        ImGui::PopID();
    }

    // End the child window with a newline if needed
    if (col != 0) {
        ImGui::NewLine();
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Texture cache helpers
// ---------------------------------------------------------------------------

void MaterialsPanel::clearTextureCache() {
    for (auto& [id, tex] : m_thumbnailCache) {
        if (tex != 0) {
            glDeleteTextures(1, &tex);
        }
    }
    m_thumbnailCache.clear();
}

GLuint MaterialsPanel::loadTGATexture(const Path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return 0;
    }

    uint8_t header[18];
    file.read(reinterpret_cast<char*>(header), 18);
    if (!file) {
        return 0;
    }

    // Only support uncompressed true-colour 32bpp TGA
    if (header[2] != 2 || header[16] != 32) {
        return 0;
    }

    int width = header[12] | (header[13] << 8);
    int height = header[14] | (header[15] << 8);
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        return 0;
    }

    size_t dataSize = static_cast<size_t>(width) * height * 4;
    std::vector<uint8_t> bgra(dataSize);
    file.read(reinterpret_cast<char*>(bgra.data()), static_cast<std::streamsize>(dataSize));
    if (!file) {
        return 0;
    }

    // Convert BGRA -> RGBA in-place
    for (size_t i = 0; i < dataSize; i += 4) {
        std::swap(bgra[i + 0], bgra[i + 2]);
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, bgra.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    return texture;
}

GLuint MaterialsPanel::getThumbnailTexture(const MaterialRecord& material) {
    auto it = m_thumbnailCache.find(material.id);
    if (it != m_thumbnailCache.end()) {
        return it->second;
    }

    GLuint tex = 0;

    // Try loading thumbnail from explicit path (TGA)
    if (tex == 0 && !material.thumbnailPath.empty()) {
        tex = loadTGATexture(material.thumbnailPath);
    }

    // Try extracting texture from .dwmat archive
    if (tex == 0 && !material.archivePath.empty()) {
        auto data = MaterialArchive::load(
            PathResolver::resolve(material.archivePath, PathCategory::Materials).string());
        if (data && !data->textureData.empty()) {
            tex = loadPNGTexture(data->textureData.data(), data->textureData.size());
        }
    }

    m_thumbnailCache[material.id] = tex;
    return tex;
}

GLuint MaterialsPanel::loadPNGTexture(const uint8_t* data, size_t size) {
    auto texData = TextureLoader::loadPNGFromMemory(data, size);
    if (!texData || texData->pixels.empty()) {
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 texData->width,
                 texData->height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 texData->pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    return texture;
}

// ---------------------------------------------------------------------------
// Category helpers
// ---------------------------------------------------------------------------

const char* MaterialsPanel::categoryInitial(MaterialCategory cat) {
    switch (cat) {
    case MaterialCategory::Hardwood:
        return "H";
    case MaterialCategory::Softwood:
        return "S";
    case MaterialCategory::Domestic:
        return "D";
    case MaterialCategory::Composite:
        return "C";
    default:
        return "?";
    }
}

ImU32 MaterialsPanel::categoryPlaceholderColor(MaterialCategory cat) {
    switch (cat) {
    case MaterialCategory::Hardwood:
        return IM_COL32(120, 80, 40, 220); // warm brown
    case MaterialCategory::Softwood:
        return IM_COL32(60, 100, 60, 220); // forest green
    case MaterialCategory::Domestic:
        return IM_COL32(90, 70, 110, 220); // muted purple
    case MaterialCategory::Composite:
        return IM_COL32(60, 80, 100, 220); // slate blue
    default:
        return IM_COL32(80, 80, 80, 220);
    }
}

} // namespace dw

#include "library_panel.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <imgui.h>

#include "../../core/config/config.h"
#include "../../core/loaders/texture_loader.h"
#include "../../core/paths/app_paths.h"
#include "../../core/utils/file_utils.h"
#include "../../core/utils/log.h"
#include "../context_menu_manager.h"
#include "../icons.h"
#include "../ui_colors.h"
#include "../widgets/toast.h"

namespace dw {

LibraryPanel::LibraryPanel(LibraryManager* library) : Panel("Library"), m_library(library) {
    m_thumbnailSize = Config::instance().getLibraryThumbSize();
    refresh();
}

LibraryPanel::~LibraryPanel() {
    clearTextureCache();
    if (m_placeholderTexture != 0) {
        glDeleteTextures(1, &m_placeholderTexture);
    }
}

GLuint LibraryPanel::getPlaceholderTexture() {
    if (m_placeholderLoaded) {
        return m_placeholderTexture;
    }
    m_placeholderLoaded = true;

    Path iconPath = paths::getBundledIconsDir() / "statue.png";
    auto data = TextureLoader::loadPNG(iconPath);
    if (!data) {
        log::warning("Library", "Failed to load placeholder icon: statue.png");
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, data->width, data->height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data->pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    m_placeholderTexture = tex;
    return tex;
}

void LibraryPanel::setContextMenuManager(ContextMenuManager* mgr) {
    m_contextMenuManager = mgr;
}

void LibraryPanel::clearTextureCache() {
    for (auto& [id, tex] : m_textureCache) {
        if (tex != 0) {
            glDeleteTextures(1, &tex);
        }
    }
    m_textureCache.clear();
}

GLuint LibraryPanel::loadTGATexture(const Path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        log::warningf("Library", "Failed to open TGA file: %s", path.string().c_str());
        return 0;
    }

    // Read 18-byte TGA header
    uint8_t header[18];
    file.read(reinterpret_cast<char*>(header), 18);
    if (!file) {
        log::warningf("Library", "Failed to read TGA header: %s", path.string().c_str());
        return 0;
    }

    // Validate: uncompressed true-color (type 2), 32bpp
    if (header[2] != 2 || header[16] != 32) {
        log::warningf("Library",
                      "Unsupported TGA format (type=%d, bpp=%d): %s",
                      header[2],
                      header[16],
                      path.string().c_str());
        return 0;
    }

    int width = header[12] | (header[13] << 8);
    int height = header[14] | (header[15] << 8);
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        log::warningf(
            "Library", "Invalid TGA dimensions (%dx%d): %s", width, height, path.string().c_str());
        return 0;
    }

    // Read BGRA pixel data
    size_t dataSize = static_cast<size_t>(width) * height * 4;
    std::vector<uint8_t> bgra(dataSize);
    file.read(reinterpret_cast<char*>(bgra.data()), static_cast<std::streamsize>(dataSize));
    if (!file) {
        log::warningf("Library", "Failed to read TGA pixel data: %s", path.string().c_str());
        return 0;
    }

    // Convert BGRA to RGBA in-place
    for (size_t i = 0; i < dataSize; i += 4) {
        std::swap(bgra[i + 0], bgra[i + 2]); // swap B and R
    }

    // Create OpenGL texture
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        log::warning("Library", "Failed to create GL texture for thumbnail");
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

GLuint LibraryPanel::getThumbnailTexture(const ModelRecord& model) {
    // Check cache first
    auto it = m_textureCache.find(model.id);
    if (it != m_textureCache.end()) {
        return it->second;
    }

    // No thumbnail path yet — don't cache so we re-check after generation
    if (model.thumbnailPath.empty()) {
        return 0;
    }

    // Try to load the TGA file
    GLuint tex = loadTGATexture(model.thumbnailPath);
    m_textureCache[model.id] = tex;
    return tex;
}

void LibraryPanel::render() {
    if (!m_open) {
        return;
    }

    // Debounce timer for FTS search
    if (m_searchDirty && m_searchDebounceTimer > 0) {
        m_searchDebounceTimer -= ImGui::GetIO().DeltaTime;
        if (m_searchDebounceTimer <= 0) {
            m_searchDirty = false;
            refresh();
        }
    }

    applyMinSize(18, 12);
    if (ImGui::Begin(m_title.c_str(), &m_open)) {
        renderToolbar();
        ImGui::Separator();
        renderTabs();
        ImGui::Separator();

        // Category breadcrumb when filtering
        renderCategoryBreadcrumb();

        // Side-by-side layout: category sidebar + content
        float availH = ImGui::GetContentRegionAvail().y;

        // Fit sidebar to widest category name
        const auto& style = ImGui::GetStyle();
        float sidebarW = ImGui::CalcTextSize("All Models").x;
        float indent = style.IndentSpacing;
        for (const auto& cat : m_categories) {
            float w = ImGui::CalcTextSize(cat.name.c_str()).x;
            if (cat.parentId.has_value())
                w += indent; // child items are indented
            sidebarW = std::max(sidebarW, w);
        }
        sidebarW += style.WindowPadding.x * 2 + style.FramePadding.x * 2;
        ImGui::BeginChild("CategorySidebar", ImVec2(sidebarW, availH), true);
        renderCategoryFilter();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("ContentArea", ImVec2(0, availH), false);
        switch (m_activeTab) {
        case ViewTab::Models:
            renderModelList();
            break;
        case ViewTab::GCode:
            renderGCodeList();
            break;
        case ViewTab::All:
            renderCombinedList();
            break;
        }
        ImGui::EndChild();

        renderRenameDialog();
        renderDeleteConfirm();
        renderCategoryAssignDialog();
    }
    ImGui::End();
}

GLuint LibraryPanel::getThumbnailTextureForModel(int64_t modelId) const {
    auto it = m_textureCache.find(modelId);
    return (it != m_textureCache.end()) ? it->second : 0;
}

void LibraryPanel::invalidateThumbnail(int64_t modelId) {
    auto it = m_textureCache.find(modelId);
    if (it != m_textureCache.end()) {
        if (it->second != 0)
            glDeleteTextures(1, &it->second);
        m_textureCache.erase(it);
    }
}

void LibraryPanel::refresh() {
    if (!m_library)
        return;

    // Refresh category cache
    m_categories = m_library->getAllCategories();

    // Determine model list based on search + category filter
    if (!m_searchQuery.empty()) {
        // FTS5 search with BM25 ranking (falls back to LIKE if FTS unavailable)
        if (m_useFTS) {
            m_models = m_library->searchModelsFTS(m_searchQuery);
        } else {
            m_models = m_library->searchModels(m_searchQuery);
        }

        // If also filtering by category, client-side filter the FTS results
        if (m_selectedCategoryId > 0) {
            auto categoryModels = m_library->filterByCategory(m_selectedCategoryId);
            std::set<int64_t> catIds;
            for (auto& m : categoryModels)
                catIds.insert(m.id);
            m_models.erase(std::remove_if(m_models.begin(),
                                          m_models.end(),
                                          [&](const ModelRecord& m) {
                                              return catIds.find(m.id) == catIds.end();
                                          }),
                           m_models.end());
        }
    } else if (m_selectedCategoryId > 0) {
        m_models = m_library->filterByCategory(m_selectedCategoryId);
    } else {
        m_models = m_library->getAllModels();
    }

    // G-code files are not affected by category filter
    if (!m_searchQuery.empty()) {
        m_gcodeFiles = m_library->searchGCodeFiles(m_searchQuery);
    } else {
        m_gcodeFiles = m_library->getAllGCodeFiles();
    }
}

void LibraryPanel::renderToolbar() {
    float avail = ImGui::GetContentRegionAvail().x;
    float style = ImGui::GetStyle().ItemSpacing.x;

    // Calculate actual button widths dynamically
    float refreshBtnW = ImGui::CalcTextSize(Icons::Refresh).x +
                        ImGui::GetStyle().FramePadding.x * 2;
    const char* viewIcon = m_showThumbnails ? Icons::Grid : Icons::List;
    float viewBtnW = ImGui::CalcTextSize(viewIcon).x + ImGui::GetStyle().FramePadding.x * 2;

    // Zoom slider width (only in grid view)
    float zoomSliderW = m_showThumbnails ? 60.0f : 0.0f;

    float buttonsW = refreshBtnW + viewBtnW + style * 2; // 2 SameLine gaps
    if (m_showThumbnails)
        buttonsW += zoomSliderW + style;

    // Search input takes remaining space after buttons
    float searchWidth = avail - buttonsW;
    if (searchWidth < 50.0f) {
        searchWidth = 50.0f;
    }

    ImGui::SetNextItemWidth(searchWidth);
    if (ImGui::InputTextWithHint(
            "##Search",
            "Search library...",
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
        // Debounce: reset timer on each keystroke, refresh fires when timer expires
        m_searchDirty = true;
        m_searchDebounceTimer = 0.2f; // 200ms debounce
    }

    ImGui::SameLine();

    // Refresh button
    if (ImGui::Button(Icons::Refresh)) {
        refresh();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Refresh library");
    }

    ImGui::SameLine();

    // View toggle
    if (ImGui::Button(viewIcon)) {
        m_showThumbnails = !m_showThumbnails;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(m_showThumbnails ? "List view" : "Grid view");
    }

    // Zoom slider (grid view only)
    if (m_showThumbnails) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(zoomSliderW);
        ImGui::SliderFloat(
            "##Zoom", &m_thumbnailSize, THUMB_MIN, avail, "", ImGuiSliderFlags_NoRoundToFormat);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            Config::instance().setLibraryThumbSize(m_thumbnailSize);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Thumbnail size (%.0fpx)", m_thumbnailSize);
        }
    }
}

void LibraryPanel::renderTabs() {
    if (ImGui::BeginTabBar("LibraryTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("All")) {
            m_activeTab = ViewTab::All;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Models")) {
            m_activeTab = ViewTab::Models;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("G-code")) {
            m_activeTab = ViewTab::GCode;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void LibraryPanel::renderCategoryFilter() {
    // Defer refresh until after iteration to avoid invalidating m_categories references
    bool needsRefresh = false;

    // "All Models" button to clear filter
    bool allSelected = (m_selectedCategoryId == -1);
    if (ImGui::Selectable("All Models", allSelected)) {
        m_selectedCategoryId = -1;
        m_selectedCategoryName.clear();
        needsRefresh = true;
    }

    ImGui::Separator();

    // Build category tree (2-level max)
    // Roots: categories with no parent
    for (auto& cat : m_categories) {
        if (cat.parentId.has_value())
            continue; // skip children in root loop

        bool isSelected = (m_selectedCategoryId == cat.id);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
        if (isSelected)
            flags |= ImGuiTreeNodeFlags_Selected;

        // Check if this root has children
        bool hasChildren = false;
        for (auto& child : m_categories) {
            if (child.parentId.has_value() && *child.parentId == cat.id) {
                hasChildren = true;
                break;
            }
        }
        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf;

        bool open = ImGui::TreeNodeEx(cat.name.c_str(), flags);
        if (ImGui::IsItemClicked()) {
            m_selectedCategoryId = cat.id;
            m_selectedCategoryName = cat.name;
            needsRefresh = true;
        }
        if (open) {
            for (auto& child : m_categories) {
                if (!child.parentId.has_value() || *child.parentId != cat.id)
                    continue;
                bool childSelected = (m_selectedCategoryId == child.id);
                if (ImGui::Selectable(child.name.c_str(), childSelected)) {
                    m_selectedCategoryId = child.id;
                    m_selectedCategoryName = cat.name + " > " + child.name;
                    needsRefresh = true;
                }
            }
            ImGui::TreePop();
        }
    }

    if (needsRefresh)
        refresh();
}

void LibraryPanel::renderCategoryBreadcrumb() {
    if (m_selectedCategoryId <= 0)
        return;

    ImGui::TextColored(colors::kInfo,
                       "Category: %s",
                       m_selectedCategoryName.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("x##clearCat")) {
        m_selectedCategoryId = -1;
        m_selectedCategoryName.clear();
        refresh();
    }
    ImGui::Separator();
}

void LibraryPanel::renderCategoryAssignDialog() {
    if (m_showCategoryAssignDialog) {
        ImGui::OpenPopup("Assign Category");
        m_showCategoryAssignDialog = false;

        // Initialize: load which categories the selected model(s) belong to
        m_assignedCategoryIds.clear();
        if (m_library && m_selectedModelIds.size() == 1) {
            int64_t modelId = *m_selectedModelIds.begin();
            // Check each category to see if this model is in it
            for (auto& cat : m_categories) {
                auto models = m_library->filterByCategory(cat.id);
                for (auto& m : models) {
                    if (m.id == modelId) {
                        m_assignedCategoryIds.insert(cat.id);
                        break;
                    }
                }
            }
        }
        m_newCategoryName[0] = '\0';
        m_newCategoryParent = -1;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const auto* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.25f, vp->WorkSize.y * 0.4f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Assign Category", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Select categories for model:");
        ImGui::Separator();

        ImGui::BeginChild("CatList", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.6f), true);

        // Show categories as checkable tree
        for (auto& cat : m_categories) {
            if (cat.parentId.has_value())
                continue; // roots only at top level

            bool checked = m_assignedCategoryIds.count(cat.id) > 0;
            if (ImGui::Checkbox(cat.name.c_str(), &checked)) {
                if (checked)
                    m_assignedCategoryIds.insert(cat.id);
                else
                    m_assignedCategoryIds.erase(cat.id);
            }

            // Show children indented
            ImGui::Indent(20.0f);
            for (auto& child : m_categories) {
                if (!child.parentId.has_value() || *child.parentId != cat.id)
                    continue;
                bool childChecked = m_assignedCategoryIds.count(child.id) > 0;
                if (ImGui::Checkbox(child.name.c_str(), &childChecked)) {
                    if (childChecked)
                        m_assignedCategoryIds.insert(child.id);
                    else
                        m_assignedCategoryIds.erase(child.id);
                }
            }
            ImGui::Unindent(20.0f);
        }

        ImGui::EndChild();

        // Quick add new category
        ImGui::Separator();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        ImGui::InputTextWithHint(
            "##NewCat", "New category name...", m_newCategoryName, sizeof(m_newCategoryName));
        ImGui::SameLine();
        if (ImGui::Button("Add") && m_newCategoryName[0] != '\0') {
            std::optional<int64_t> parentId;
            if (m_newCategoryParent > 0)
                parentId = m_newCategoryParent;
            auto newId = m_library->createCategory(m_newCategoryName, parentId);
            if (newId) {
                m_categories = m_library->getAllCategories();
                m_assignedCategoryIds.insert(*newId);
            }
            m_newCategoryName[0] = '\0';
        }

        ImGui::Separator();
        float dlgBtnW = ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 4;
        if (ImGui::Button("Apply", ImVec2(dlgBtnW, 0))) {
            // Apply category assignments for selected model(s)
            if (m_library) {
                for (int64_t modelId : m_selectedModelIds) {
                    // Remove categories no longer checked
                    for (auto& cat : m_categories) {
                        bool wasAssigned = false;
                        auto models = m_library->filterByCategory(cat.id);
                        for (auto& m : models) {
                            if (m.id == modelId) {
                                wasAssigned = true;
                                break;
                            }
                        }
                        bool isNowAssigned = m_assignedCategoryIds.count(cat.id) > 0;

                        if (wasAssigned && !isNowAssigned) {
                            m_library->removeModelCategory(modelId, cat.id);
                        } else if (!wasAssigned && isNowAssigned) {
                            m_library->assignCategory(modelId, cat.id);
                        }
                    }
                }
                refresh();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(dlgBtnW, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// renderModelList, renderModelItem, renderGCodeList, renderGCodeItem,
// renderCombinedList, and registerContextMenuEntries are in library_panel_items.cpp

void LibraryPanel::renderDeleteConfirm() {
    if (m_showDeleteConfirm) {
        ImGui::OpenPopup("Delete Item?");
        m_showDeleteConfirm = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Delete Item?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        float dlgBtnW = ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 4;
        ImGui::Text("Delete \"%s\"?", m_deleteItemName.c_str());
        ImGui::TextDisabled("This action cannot be undone.");
        ImGui::Spacing();

        if (ImGui::Button("Delete", ImVec2(dlgBtnW, 0))) {
            if (m_library) {
                for (int64_t id : m_deleteItemIds) {
                    if (m_deleteIsGCode) {
                        m_library->deleteGCodeFile(id);
                    } else {
                        m_library->removeModel(id);
                    }
                }
                size_t count = m_deleteItemIds.size();
                if (m_deleteIsGCode) {
                    ToastManager::instance().show(ToastType::Success,
                                                  "Deleted",
                                                  count == 1 ? "G-code file deleted successfully"
                                                             : std::to_string(count) +
                                                                   " G-code files deleted");
                } else {
                    ToastManager::instance().show(ToastType::Success,
                                                  "Deleted",
                                                  count == 1
                                                      ? "Model deleted successfully"
                                                      : std::to_string(count) + " models deleted");
                }
                // Clear selection for deleted items
                if (m_deleteIsGCode) {
                    m_selectedGCodeIds.clear();
                    m_lastClickedGCodeId = -1;
                } else {
                    m_selectedModelIds.clear();
                    m_lastClickedModelId = -1;
                }
                refresh();
            } else {
                ToastManager::instance().show(ToastType::Error,
                                              "Delete Failed",
                                              "Could not delete item");
            }
            m_deleteItemIds.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(dlgBtnW, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void LibraryPanel::renderRenameDialog() {
    if (m_showRenameDialog) {
        ImGui::OpenPopup("Rename Model");
        m_showRenameDialog = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetMainViewport()->WorkSize.x * 0.25f, 0), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Rename Model", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        float dlgBtnW = ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 4;
        ImGui::Text("Enter new name:");
        ImGui::SetNextItemWidth(-1);

        bool enterPressed = ImGui::InputText("##RenameInput",
                                             m_renameBuffer,
                                             sizeof(m_renameBuffer),
                                             ImGuiInputTextFlags_EnterReturnsTrue);

        // Auto-focus the input field when popup opens
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere(-1);
        }

        ImGui::Spacing();

        bool okPressed = ImGui::Button("OK", ImVec2(dlgBtnW, 0));
        ImGui::SameLine();
        bool cancelPressed = ImGui::Button("Cancel", ImVec2(dlgBtnW, 0));

        if (okPressed || enterPressed) {
            std::string newName(m_renameBuffer);
            // Trim whitespace
            newName.erase(0, newName.find_first_not_of(" \t\n\r"));
            newName.erase(newName.find_last_not_of(" \t\n\r") + 1);

            if (newName.empty()) {
                ToastManager::instance().show(ToastType::Warning,
                                              "Invalid Name",
                                              "Name cannot be empty");
            } else if (m_library) {
                auto record = m_library->getModel(m_renameModelId);
                if (record) {
                    record->name = newName;
                    if (m_library->updateModel(*record)) {
                        refresh();
                        ToastManager::instance().show(ToastType::Success,
                                                      "Renamed",
                                                      "Model renamed successfully");
                    } else {
                        ToastManager::instance().show(ToastType::Error,
                                                      "Rename Failed",
                                                      "Could not rename model");
                        log::error("Library", "Failed to rename model");
                    }
                }
            }
            ImGui::CloseCurrentPopup();
        }

        if (cancelPressed) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

} // namespace dw

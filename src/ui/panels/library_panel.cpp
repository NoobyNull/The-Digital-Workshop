#include "library_panel.h"

#include <algorithm>
#include <vector>

#include <imgui.h>

#include "../../core/config/config.h"
#include "../icons.h"
#include "../ui_colors.h"

namespace dw {

namespace {

constexpr int kNeedsRetagStatus = 4;

void retainTagStatus(std::vector<ModelRecord>& models, int status) {
    models.erase(std::remove_if(models.begin(),
                                models.end(),
                                [status](const ModelRecord& model) {
                                    return model.tagStatus != status;
                                }),
                 models.end());
}

} // namespace

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

void LibraryPanel::setContextMenuManager(ContextMenuManager* mgr) {
    m_contextMenuManager = mgr;
}

void LibraryPanel::render() {
    if (!m_open) {
        return;
    }
    const bool wasOpen = m_open;

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
        const bool suppressPickerInput = m_pickerInputSuppressionFrames > 0;
        if (suppressPickerInput)
            ImGui::BeginDisabled();
        renderPickerHeader();
        renderToolbar();
        ImGui::Separator();
        if (isProjectModelPicker()) {
            ImGui::TextDisabled("Models");
        } else {
            renderTabs();
        }
        ImGui::Separator();

        // Category breadcrumb when filtering
        renderCategoryBreadcrumb();

        // Side-by-side layout: category sidebar + content
        float availH = ImGui::GetContentRegionAvail().y;

        // Fit sidebar to widest category name
        const auto& style = ImGui::GetStyle();
        float sidebarW = ImGui::CalcTextSize("Needs Retag").x;
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
        switch (isProjectModelPicker() ? ViewTab::Models : m_activeTab) {
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
        if (suppressPickerInput)
            ImGui::EndDisabled();
    }
    ImGui::End();
    if (m_pickerInputSuppressionFrames > 0)
        --m_pickerInputSuppressionFrames;

    if (wasOpen && !m_open && m_pickerSnapshot.active) {
        const auto result = emitLibraryIntent(design_library::LibraryCancelRequested{});
        const auto* action = std::get_if<design_library::LibraryActionResult>(&result);
        if (!action || action->status == design_library::LibraryActionResultStatus::Rejected)
            m_open = true;
    }
}

void LibraryPanel::refresh() {
    if (!m_library)
        return;

    // Refresh category cache
    auto allCategories = m_library->getAllCategories();
    m_categories.clear();
    for (const auto& cat : allCategories) {
        if (!m_library->filterByCategory(cat.id).empty())
            m_categories.push_back(cat);
    }
    if (m_selectedCategoryId > 0 &&
        std::none_of(m_categories.begin(), m_categories.end(), [this](const CategoryRecord& cat) {
            return cat.id == m_selectedCategoryId;
        })) {
        m_selectedCategoryId = -1;
        m_selectedCategoryName.clear();
    }

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
    } else if (m_tagReviewFilter == TagReviewFilter::NeedsRetag) {
        m_models = m_library->filterByTagStatus(kNeedsRetagStatus);
    } else {
        m_models = m_library->getAllModels();
    }

    if (m_tagReviewFilter == TagReviewFilter::NeedsRetag) {
        retainTagStatus(m_models, kNeedsRetagStatus);
    }

    // G-code files are not affected by category filter, but tag review filters
    // apply only to models and should hide G-code noise.
    if (m_tagReviewFilter == TagReviewFilter::NeedsRetag) {
        m_gcodeFiles.clear();
    } else if (!m_searchQuery.empty()) {
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
                    str->resize(static_cast<std::string::size_type>(data->BufTextLen));
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
            ImGui::SetTooltip("Thumbnail size (%.0fpx)", static_cast<double>(m_thumbnailSize));
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
    bool allSelected =
        (m_selectedCategoryId == -1 && m_tagReviewFilter == TagReviewFilter::All);
    if (ImGui::Selectable("All Models", allSelected)) {
        m_selectedCategoryId = -1;
        m_selectedCategoryName.clear();
        m_tagReviewFilter = TagReviewFilter::All;
        needsRefresh = true;
    }

    bool needsRetagSelected = (m_tagReviewFilter == TagReviewFilter::NeedsRetag);
    if (ImGui::Selectable("Needs Retag", needsRetagSelected)) {
        m_tagReviewFilter = needsRetagSelected ? TagReviewFilter::All
                                               : TagReviewFilter::NeedsRetag;
        m_activeTab = ViewTab::Models;
        needsRefresh = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show models that need manual AI retagging");
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
    if (m_selectedCategoryId <= 0 && m_tagReviewFilter == TagReviewFilter::All)
        return;

    if (m_selectedCategoryId > 0) {
        ImGui::TextColored(colors::kInfo,
                           "Category: %s",
                           m_selectedCategoryName.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("x##clearCat")) {
            m_selectedCategoryId = -1;
            m_selectedCategoryName.clear();
            refresh();
        }
    }

    if (m_tagReviewFilter == TagReviewFilter::NeedsRetag) {
        if (m_selectedCategoryId > 0)
            ImGui::SameLine();
        ImGui::TextColored(colors::kWarning, "Filter: Needs Retag");
        ImGui::SameLine();
        if (ImGui::SmallButton("x##clearRetag")) {
            m_tagReviewFilter = TagReviewFilter::All;
            refresh();
        }
    }
    ImGui::Separator();
}

// Item rendering, picker presentation, actions, and dialogs live in focused
// LibraryPanel translation units.

} // namespace dw

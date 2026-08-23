#pragma once

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glad/gl.h>

#include "../../core/library/library_manager.h"
#include "../../modules/design_library/library_picker_presentation.h"
#include "../../modules/design_library/ui/library_picker_view.h"
#include "panel.h"

namespace dw {

class ContextMenuManager;

// Library panel for browsing and managing imported models
class LibraryPanel : public Panel {
  public:
    explicit LibraryPanel(LibraryManager* library);
    ~LibraryPanel() override;

    void render() override;

    // The panel owns selection and presentation only. Application policy
    // receives typed intents and returns explicit action/deletion outcomes.
    using LibraryIntentCallback = std::function<design_library::LibraryPanelIntentResult(
        const design_library::LibraryPanelIntent& intent)>;
    void setOnLibraryIntent(LibraryIntentCallback callback) {
        m_onLibraryIntent = std::move(callback);
    }
    void setPickerState(const design_library::LibraryPickerSnapshot& snapshot,
                        std::string errorMessage = {});
    void clearPickerState();
    [[nodiscard]] const design_library::LibraryPickerSnapshot& pickerState() const noexcept {
        return m_pickerSnapshot;
    }

    // Library maintenance callbacks are intentionally separate from picker
    // selection/navigation intent.
    using ModelActionCallback = std::function<void(int64_t modelId)>;

    // Callback when thumbnail regeneration is requested (batch: all selected IDs)
    using RegenerateThumbnailCallback = std::function<void(const std::vector<int64_t>& modelIds)>;
    void setOnRegenerateThumbnail(RegenerateThumbnailCallback callback) {
        m_onRegenerateThumbnail = std::move(callback);
    }

    // Callback when "Assign Default Material" is chosen
    void setOnAssignDefaultMaterial(ModelActionCallback callback) {
        m_onAssignDefaultMaterial = std::move(callback);
    }

    // Callback when "Tag Image" is chosen (one or more model IDs)
    using TagImageCallback = std::function<void(const std::vector<int64_t>& modelIds)>;
    void setOnTagImage(TagImageCallback callback) { m_onTagImage = std::move(callback); }

    // Refresh the model and G-code lists
    void refresh();

    // Invalidate cached thumbnail texture for a model (forces reload from disk)
    void invalidateThumbnail(int64_t modelId);

    // Get/set currently selected model ID (-1 if none)
    int64_t selectedModelId() const { return m_lastClickedModelId; }
    void setSelectedModelId(int64_t id);

    // Multi-selection accessors
    const std::set<int64_t>& selectedModelIds() const { return m_selectedModelIds; }
    const std::set<int64_t>& selectedGCodeIds() const { return m_selectedGCodeIds; }
    bool isModelSelected(int64_t id) const { return m_selectedModelIds.count(id) > 0; }

    // Get cached thumbnail GL texture for a model (0 if not cached)
    GLuint getThumbnailTextureForModel(int64_t modelId) const;

    // Set context menu manager (must be called before first render)
    void setContextMenuManager(ContextMenuManager* mgr);

  private:
    enum class ViewTab { All, Models, GCode };
    enum class TagReviewFilter { All, NeedsRetag };

    void renderToolbar();
    void renderTabs();
    void renderCategoryFilter();
    void renderCategoryBreadcrumb();
    void renderCategoryAssignDialog();
    void renderModelList();
    void renderGCodeList();
    void renderCombinedList();
    void renderModelItem(const ModelRecord& model, int index, float thumbOverride = 0.0f);
    void renderGCodeItem(const GCodeRecord& gcode, int index, float thumbOverride = 0.0f);
    bool handleModelClick(const ModelRecord& model);
    bool handleGCodeClick(const GCodeRecord& gcode);
    void renderRenameDialog();
    void renderDeleteConfirm();
    void registerContextMenuEntries();
    void renderPickerHeader();
    void emitSelectionChanged();
    void emitPreviewRequested(workshop::LibraryItemRef item);
    [[nodiscard]] design_library::LibraryPanelIntentResult emitLibraryIntent(
        const design_library::LibraryPanelIntent& intent);
    [[nodiscard]] std::vector<workshop::LibraryItemRef> selectedLibraryItems() const;
    [[nodiscard]] std::optional<workshop::LibraryItemRef> focusedLibraryItem() const;
    [[nodiscard]] std::string focusedLibraryItemName() const;
    [[nodiscard]] bool isPickerProjectMember(workshop::LibraryItemRef item) const;
    [[nodiscard]] bool isProjectModelPicker() const noexcept;
    void applyPickerSelection(const std::vector<workshop::LibraryItemRef>& items);
    void revealPendingSelection(workshop::LibraryItemRef item);
    void applyConfirmedDeletion(
        const std::vector<workshop::LibraryItemRef>& confirmedItems);

    // Load a TGA file into an OpenGL texture, returns 0 on failure
    GLuint loadTGATexture(const Path& path);

    // Get or load a cached thumbnail texture for a model
    GLuint getThumbnailTexture(const ModelRecord& model);

    // Release all cached GL textures
    void clearTextureCache();

    LibraryManager* m_library;
    std::vector<ModelRecord> m_models;
    std::vector<GCodeRecord> m_gcodeFiles;
    std::string m_searchQuery;
    std::set<int64_t> m_selectedModelIds;
    std::set<int64_t> m_selectedGCodeIds;
    int64_t m_lastClickedModelId = -1;
    int64_t m_lastClickedGCodeId = -1;
    std::optional<workshop::LibraryItemRef> m_pendingSelectionReveal;

    ViewTab m_activeTab = ViewTab::All;

    LibraryIntentCallback m_onLibraryIntent;
    RegenerateThumbnailCallback m_onRegenerateThumbnail;
    ModelActionCallback m_onAssignDefaultMaterial;
    TagImageCallback m_onTagImage;

    design_library::LibraryPickerSnapshot m_pickerSnapshot;
    design_library::LibraryPickerView m_pickerView;
    std::string m_pickerError;
    int m_pickerInputSuppressionFrames = 0;

    // Thumbnail texture cache: model ID -> GL texture
    std::unordered_map<int64_t, GLuint> m_textureCache;

    // Placeholder texture for models without a thumbnail (statue.png)
    GLuint m_placeholderTexture = 0;
    bool m_placeholderLoaded = false;
    GLuint getPlaceholderTexture();

    // Rename dialog state
    bool m_showRenameDialog = false;
    int64_t m_renameModelId = 0;
    char m_renameBuffer[256] = {};

    // Delete confirmation state
    bool m_showDeleteConfirm = false;
    std::vector<workshop::LibraryItemRef> m_deleteItems;
    std::string m_deleteItemName; // Single item name, or "N items" for batch
    std::string m_deleteResultMessage;
    design_library::LibraryDeleteResultStatus m_deleteResultStatus =
        design_library::LibraryDeleteResultStatus::Failed;

    // View options
    bool m_showThumbnails = true;
    float m_thumbnailSize = 96.0f;
    static constexpr float THUMB_MIN = 48.0f;
    // THUMB_MAX is computed per-frame as the available content width

    // Category filter state
    int64_t m_selectedCategoryId = -1;        // -1 = show all
    std::string m_selectedCategoryName;       // For breadcrumb display
    TagReviewFilter m_tagReviewFilter = TagReviewFilter::All;
    std::vector<CategoryRecord> m_categories; // Cached category list
    bool m_showCategoryAssignDialog = false;
    float m_searchDebounceTimer = 0.0f;
    bool m_searchDirty = false;
    bool m_useFTS = true; // Use FTS5 for search (true) vs LIKE (false)

    // Category assign dialog state
    std::set<int64_t> m_assignedCategoryIds; // Categories checked for current model(s)
    char m_newCategoryName[128] = {};
    int64_t m_newCategoryParent = -1; // -1 = root

    // Context menu management
    ContextMenuManager* m_contextMenuManager = nullptr;
    std::optional<ModelRecord> m_currentContextMenuModel;
    std::optional<GCodeRecord> m_currentContextMenuGCode;
};

} // namespace dw

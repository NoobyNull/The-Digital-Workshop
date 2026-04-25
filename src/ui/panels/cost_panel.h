#pragma once

#include <string>
#include <vector>

#include <imgui.h>

#include "../../core/database/cost_repository.h"
#include "../../core/database/rate_category_repository.h"
#include "../../core/project/costing_engine.h"
#include "../../core/project/project_costing_io.h"
#include "panel.h"

namespace dw {

// View mode for the costing panel editor
enum class CostViewMode { Estimate, Order };

// Project costing panel for creating and managing cost records
class CostingPanel : public Panel {
  public:
    explicit CostingPanel(CostRepository* repo, RateCategoryRepository* rateCatRepo = nullptr);
    ~CostingPanel() override = default;

    void render() override;

    // Navigate to and select a specific record by ID
    void selectRecord(i64 recordId);

    // Set the project costing directory for persistence
    void setCostingDir(const Path& dir);

    // External CLO auto-push: add a material entry from cut optimizer
    void addCloEntry(const CostingEntry& entry);
    void upsertAutoEntry(const CostingEntry& entry, const std::string& key);

    // Persist current engine state to project costing directory
    void save();

  private:
    void renderToolbar();
    void renderRecordList();
    void renderRecordEditor();
    void renderRateCategories();

    // Category-grouped costing display
    void renderCostingSummary();
    void renderCategorySection(const std::string& category, const std::string& displayName);
    void renderAddLaborForm();
    void renderAddOverheadForm();
    void renderAddMaterialForm();

    // Estimate/Order view support
    void renderSalePriceAndMargin();
    void renderViewModeToggle();
    void renderOrderView();
    void convertEstimateToOrder();
    void loadOrdersFromDisk();
    void saveOrdersToDisk();
    void renderExportButton();

    // Sync between CostingEngine and CostingRecord
    void syncEngineFromRecord();
    void syncRecordFromEngine();

    // Persistence
    void loadFromDisk();
    void saveToDisk();
    void syncEstimateFromEngine();

    // Recalculate edit buffer totals from items
    void recalculateEditBuffer();

    // Reload effective rates from DB based on current context
    void refreshRateCategories();

    CostRepository* m_repo;
    RateCategoryRepository* m_rateCatRepo;
    std::vector<CostingRecord> m_records;
    int m_selectedIndex = -1;

    // Costing engine for category-grouped computation
    CostingEngine m_engine;

    // Persistence state
    Path m_costingDir;
    std::vector<CostingEstimate> m_estimates;
    int m_activeEstimateIndex = -1;

    // View mode
    CostViewMode m_viewMode = CostViewMode::Estimate;

    // Sale price (editable in estimate mode)
    float m_salePrice = 0.0f;

    // Order state
    std::vector<CostingOrder> m_orders;
    int m_selectedOrderIndex = -1;

    // Edit buffer for selected record
    CostingRecord m_editBuffer;
    char m_editName[256]{};
    char m_editNotes[1024]{};

    // Labor form buffers
    char m_laborName[256]{};
    float m_laborRate = 25.0f;
    float m_laborHours = 1.0f;

    // Overhead form buffers
    char m_overheadName[256]{};
    float m_overheadAmount = 0.0f;
    char m_overheadNotes[256]{};

    // Material form buffers
    char m_materialName[256]{};
    float m_materialPrice = 0.0f;
    float m_materialQty = 1.0f;
    char m_materialUnit[64]{"sheet"};

    // Tax/discount edit fields
    float m_editTaxRate = 0.0f;
    float m_editDiscountRate = 0.0f;

    bool m_isNewRecord = false;

    // Rate category state
    std::vector<RateCategory> m_effectiveRates;
    f64 m_projectVolumeCuUnits = 0.0;
    char m_newRateName[256]{};
    float m_newRatePerCuUnit = 0.0f;
    char m_newRateNotes[256]{};
    bool m_showRateCategories = true;
    int m_editingRateIndex = -1;      // Which rate row is being edited (-1 = none)
    char m_editRateName[256]{};
    float m_editRatePerCuUnit = 0.0f;
    char m_editRateNotes[256]{};
    bool m_newRateIsOverride = false;  // For project context: insert as project override?
};

} // namespace dw

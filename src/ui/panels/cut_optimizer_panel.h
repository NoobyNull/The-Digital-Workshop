#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../../core/database/stock_size_repository.h"
#include "../../core/materials/material.h"
#include "../../core/optimizer/cut_list_file.h"
#include "../../core/optimizer/cut_optimizer.h"
#include "../../core/optimizer/multi_stock_optimizer.h"
#include "../../core/optimizer/sheet.h"
#include "../../core/optimizer/waste_breakdown.h"
#include "../widgets/canvas_2d.h"
#include "panel.h"

namespace dw {

class ModelRepository;
class ProjectManager;
class CostRepository;
class MaterialManager;

// 2D Cut optimizer panel -- CutListMachine-style 3-column layout
class CutOptimizerPanel : public Panel {
  public:
    CutOptimizerPanel();
    ~CutOptimizerPanel() override = default;

    void render() override;

    // Clear all data
    void clear();

    // Load from a CutListFile::LoadResult
    void loadCutPlan(const CutListFile::LoadResult& lr);

    // Dependency injection
    void setCutListFile(CutListFile* clf) { m_cutListFile = clf; }
    void setProjectManager(ProjectManager* pm) { m_projectManager = pm; }
    void setModelRepository(ModelRepository* repo) { m_modelRepo = repo; }
    void setMaterialManager(MaterialManager* mm);

    // Each CLO material group to push as a cost entry
    struct CloGroupCostData {
        std::string materialName;  // e.g. "Baltic Birch Plywood"
        std::string dimensions;    // e.g. "1220x2440x19mm" from usedSheet
        i64 stockSizeDbId = 0;     // StockSize.id for snapshot
        int sheetsUsed = 0;
        f64 costPerSheet = 0.0;
        f64 totalCost = 0.0;
    };

    struct StockSelection {
        optimizer::Sheet sheet;
        std::optional<StockSize> stockSize;
        std::optional<MaterialRecord> material;
    };

    // Cross-panel integration
    const std::vector<optimizer::Part>& parts() const { return m_parts; }
    const optimizer::Sheet& sheet() const { return m_sheet; }
    std::optional<StockSelection> currentStockSelection() const;
    void addPart(const optimizer::Part& part);
    void upsertPart(const optimizer::Part& part);

    // Cost integration callback -- passes per-group material data
    using AddToCostCallback = std::function<void(const std::vector<CloGroupCostData>& groups)>;
    void setOnAddToCost(AddToCostCallback cb) { m_onAddToCost = std::move(cb); }

  private:
    void saveCutPlan(const char* name);
    std::string nextPartLabel() const;

    // Toolbar
    void renderToolbar();

    // Left column
    void renderCutListTable();
    void renderStockSheets();
    void renderSettings();

    // Center column
    void renderVisualization();

    // Right column
    void renderLayoutsSidebar();
    void renderResultsPanel();

    // Bottom
    void renderStatusBar();

    // Popups
    void renderImportPopup();

    void runOptimization();
    void refreshMaterials();

    // Input data
    std::vector<optimizer::Part> m_parts;
    optimizer::Sheet m_sheet{2440.0f, 1220.0f}; // Standard 4x8 sheet

    // Settings
    optimizer::Algorithm m_algorithm = optimizer::Algorithm::Guillotine;
    bool m_allowRotation = true;
    float m_kerf = 3.0f;   // mm
    float m_margin = 5.0f; // mm

    // Results (single-sheet, backward compatible)
    optimizer::CutPlan m_result;
    bool m_hasResults = false;

    // Multi-stock results
    optimizer::MultiStockResult m_multiResult;
    bool m_hasMultiResults = false;
    int m_selectedGroupIdx = 0;

    // Editor state
    int m_editPartIdx = -1;
    char m_newPartName[64] = "";
    float m_newPartWidth = 100.0f;
    float m_newPartHeight = 100.0f;
    int m_newPartQuantity = 1;

    // Visualization state
    int m_selectedSheet = 0;
    Canvas2D m_canvas;

    // Stock sheet name buffer
    char m_sheetName[64] = "";

    // Persistence
    CutListFile* m_cutListFile = nullptr;
    ProjectManager* m_projectManager = nullptr;
    Path m_loadedPlanPath;

    // Model import
    ModelRepository* m_modelRepo = nullptr;
    bool m_importPopupOpen = false;
    std::vector<char> m_importSelection;

    // Cost integration
    AddToCostCallback m_onAddToCost;

    // Material system
    MaterialManager* m_materialManager = nullptr;
    std::vector<MaterialRecord> m_materials;
    int m_selectedMaterialIdx = -1;         // Index into m_materials (-1 = none/manual)
    std::vector<StockSize> m_stockSizes;
    int m_selectedStockIdx = -1;            // -1 = auto-select best
    bool m_materialsLoaded = false;
};

} // namespace dw

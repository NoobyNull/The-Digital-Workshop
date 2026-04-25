#pragma once

#include <functional>
#include <memory>
#include <unordered_set>

#include "../../core/project/project.h"
#include "../../core/types.h"
#include "panel.h"

namespace dw {

// Forward declarations for repository types
class ModelRepository;
class GCodeRepository;
class CutPlanRepository;
class CostRepository;

// Project panel for managing current project and its assets
class ProjectPanel : public Panel {
  public:
    ProjectPanel(ProjectManager* projectManager,
                 ModelRepository* modelRepo,
                 GCodeRepository* gcodeRepo,
                 CutPlanRepository* cutPlanRepo,
                 CostRepository* costRepo);
    ~ProjectPanel() override = default;

    void render() override;

    // Callback when a project model is selected
    using ModelSelectedCallback = std::function<void(int64_t modelId)>;
    void setOnModelSelected(ModelSelectedCallback callback) {
        m_onModelSelected = std::move(callback);
    }

    // Navigation callbacks for asset sections
    using GCodeSelectedCallback = std::function<void(int64_t gcodeId)>;
    using MaterialSelectedCallback = std::function<void(int64_t materialId)>;
    using CostSelectedCallback = std::function<void(int64_t estimateId)>;
    using CutPlanSelectedCallback = std::function<void(int64_t planId)>;
    using OperationSelectedCallback = std::function<void(const ProjectOpenItem& item)>;

    void setOnGCodeSelected(GCodeSelectedCallback cb) { m_onGCodeSelected = std::move(cb); }
    void setOnMaterialSelected(MaterialSelectedCallback cb) {
        m_onMaterialSelected = std::move(cb);
    }
    void setOnCostSelected(CostSelectedCallback cb) { m_onCostSelected = std::move(cb); }
    void setOnCutPlanSelected(CutPlanSelectedCallback cb) {
        m_onCutPlanSelected = std::move(cb);
    }
    void setOnOperationSelected(OperationSelectedCallback cb) {
        m_onOperationSelected = std::move(cb);
    }

    // Callback when Open Project button is clicked in the panel
    using OpenProjectCallback = std::function<void()>;
    void setOpenProjectCallback(OpenProjectCallback callback) {
        m_openProjectCallback = std::move(callback);
    }

    // Callback when Save Project button is clicked in the panel
    using SaveProjectCallback = std::function<void()>;
    void setSaveProjectCallback(SaveProjectCallback callback) {
        m_saveProjectCallback = std::move(callback);
    }

    // Callback when Export .dwproj button is clicked
    using ExportProjectCallback = std::function<void()>;
    void setExportProjectCallback(ExportProjectCallback cb) {
        m_exportProjectCallback = std::move(cb);
    }

    // Callback when a recent project is clicked
    using RecentProjectCallback = std::function<void(const Path&)>;
    void setOnOpenRecentProject(RecentProjectCallback cb) {
        m_onOpenRecentProject = std::move(cb);
    }

  private:
    // Section renderers
    void renderProjectInfo();
    void renderWarningsSection(const std::vector<ProjectOpenItem>& items);
    void renderOpenItemsSection();
    void renderModelsSection();
    void renderGCodeSection();
    void renderMaterialsSection();
    void renderCostsSection();
    void renderCutPlansSection();
    void renderNotesSection();
    void renderNoProject();

    // Core
    ProjectManager* m_projectManager;
    int64_t m_selectedModelId = -1;

    // Repositories
    ModelRepository* m_modelRepo;
    GCodeRepository* m_gcodeRepo;
    CutPlanRepository* m_cutPlanRepo;
    CostRepository* m_costRepo;

    // Callbacks
    ModelSelectedCallback m_onModelSelected;
    GCodeSelectedCallback m_onGCodeSelected;
    MaterialSelectedCallback m_onMaterialSelected;
    CostSelectedCallback m_onCostSelected;
    CutPlanSelectedCallback m_onCutPlanSelected;
    OperationSelectedCallback m_onOperationSelected;
    OpenProjectCallback m_openProjectCallback;
    SaveProjectCallback m_saveProjectCallback;
    ExportProjectCallback m_exportProjectCallback;
    RecentProjectCallback m_onOpenRecentProject;

    // Notes editing state
    char m_notesBuf[4096] = {};
    bool m_notesChanged = false;
    int64_t m_notesProjectId = -1; // track which project notes are loaded for
};

} // namespace dw

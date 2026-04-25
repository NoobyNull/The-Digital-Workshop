#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "panel.h"

#include "core/carve/model_fitter.h"
#include "core/carve/tool_recommender.h"
#include "core/carve/toolpath_types.h"
#include "core/cnc/cnc_tool.h"
#include "core/cnc/cnc_types.h"
#include "core/materials/material.h"
#include "core/mesh/vertex.h"
#include "core/types.h"
#include "ui/dialogs/machine_profile_dialog.h"

namespace dw {

class CncController;
class FileDialog;
class GCodeRepository;
class GCodePanel;
class LibraryManager;
class MaterialManager;
class ProjectManager;
class ToolDatabase;
class ToolboxRepository;

namespace carve {
class CarveJob;
}

// Direct Carve wizard panel -- step-by-step guided workflow
// for streaming 2.5D toolpaths directly from STL models.
class DirectCarvePanel : public Panel {
  public:
    DirectCarvePanel();
    ~DirectCarvePanel() override;

    void render() override;

    // Dependencies
    void setCncController(CncController* cnc);
    void setToolDatabase(ToolDatabase* db);
    void setToolboxRepository(ToolboxRepository* repo);
    void setCarveJob(carve::CarveJob* job);
    void setFileDialog(FileDialog* dlg);
    void setGCodeRepository(GCodeRepository* repo);
    void setGCodePanel(GCodePanel* gcp);
    void setLibraryManager(LibraryManager* library);
    void setMaterialManager(MaterialManager* mgr);
    void setProjectManager(ProjectManager* pm);
    void setOpenToolBrowserCallback(std::function<void()> cb);
    void setCutOptimizerPanel(class CutOptimizerPanel* cop);

    struct MaterialPartSync {
        std::string key;
        std::string name;
        std::string materialName;
        std::string dimensions;
        i64 stockSizeDbId = 0;
        f64 unitRate = 0.0;
        f64 quantity = 1.0;
        std::string unit = "blank";
    };
    using MaterialPartSyncCallback = std::function<void(const MaterialPartSync&)>;
    void setOnMaterialPartSync(MaterialPartSyncCallback cb) {
        m_onMaterialPartSync = std::move(cb);
    }

    // FitParams change notification (for viewport alignment overlay)
    using FitParamsCallback = std::function<void(const carve::FitParams&,
                                                  const Vec3&, const Vec3&,
                                                  const carve::StockDimensions&)>;
    void setOnFitParamsChanged(FitParamsCallback cb) { m_onFitParamsChanged = std::move(cb); }

    // Callbacks
    void onConnectionChanged(bool connected);
    void onStatusUpdate(const MachineStatus& status);
    void onModelLoaded(const std::vector<Vertex>& vertices,
                       const std::vector<u32>& indices,
                       const Vec3& boundsMin,
                       const Vec3& boundsMax,
                       const std::string& modelName = "",
                       const Path& modelSourcePath = "",
                       u32 thumbnailTexture = 0);

  private:
    enum class Step {
        // --- Planning (no machine required) ---
        ModelFit,      // Scale, position, depth adjustment
        ToolSelect,    // Review/accept tool recommendations
        MaterialSetup, // Material selection, feeds confirmation
        Preview,       // Toolpath preview with time estimate
        // --- Machine (CNC required) ---
        MachineCheck,  // Verify connection, homing, profile
        ZeroConfirm,   // Verify zero position
        OutlineTest,   // Run perimeter trace at safe Z
        Commit,        // Final confirmation
        Running        // Job in progress
    };

    static constexpr int STEP_COUNT = 9;

    // Step rendering (one method per step)
    void renderMachineCheck();
    void renderModelFit();
    void renderToolSelect();
    void renderMaterialSetup();
    void renderPreview();
    void renderOutlineTest();
    void renderZeroConfirm();
    void renderCommit();
    void renderRunning();

    // Tool selection helpers
    void renderToolLibraryPicker();
    void renderManualToolEntry();

    // Navigation
    void renderStepIndicator();
    void renderNavButtons();
    bool canAdvance() const;
    void advanceStep();
    void retreatStep();

    // Validation
    bool validateMachineReady() const;

    // Step label helper
    static const char* stepLabel(Step step);

    // State
    Step m_currentStep = Step::ModelFit;
    CncController* m_cnc = nullptr;
    ToolDatabase* m_toolDb = nullptr;
    ToolboxRepository* m_toolboxRepo = nullptr;
    carve::CarveJob* m_carveJob = nullptr;
    FileDialog* m_fileDialog = nullptr;
    GCodeRepository* m_gcodeRepo = nullptr;
    GCodePanel* m_gcodePanel = nullptr;
    LibraryManager* m_libraryManager = nullptr;
    MachineStatus m_machineStatus;
    bool m_cncConnected = false;

    // Machine check state
    bool m_safeZConfirmed = false;

    // Model data (set via onModelLoaded)
    std::vector<Vertex> m_vertices;
    std::vector<u32> m_indices;
    bool m_modelLoaded = false;
    Vec3 m_modelBoundsMin{0.0f};
    Vec3 m_modelBoundsMax{0.0f};

    // Per-step state (populated as wizard progresses)
    carve::FitParams m_fitParams;
    carve::ToolpathConfig m_toolpathConfig;
    carve::StockDimensions m_stock;
    carve::ModelFitter m_fitter;

    // Tool selection state
    VtdbToolGeometry m_finishTool;
    VtdbToolGeometry m_clearTool;
    bool m_finishingToolSelected = false;
    bool m_clearToolSelected = false;
    bool m_toolLibraryLoaded = false;
    std::vector<VtdbToolGeometry> m_libraryTools;  // Currently displayed tool list
    std::vector<VtdbToolGeometry> m_toolboxTools;  // My Toolbox subset
    std::vector<VtdbToolGeometry> m_allTools;      // Full library
    bool m_showAllTools = false;                   // false = My Toolbox, true = All Tools
    int m_selectedLibToolIdx = -1;
    bool m_useManualTool = false;
    // Tool recommendation state
    carve::RecommendationResult m_recommendation;
    bool m_recommendationRun = false;
    int m_selectedClearIdx = -1;
    // Manual tool entry fields
    int m_manualToolType = 0;   // 0=BallNose, 1=VBit, 2=EndMill, 3=TaperedBallNose
    f32 m_manualDiameter = 3.175f;  // 1/8" default
    f32 m_manualAngle = 90.0f;
    f32 m_manualTipRadius = 1.5875f;
    int m_manualFlutes = 2;

    // Material state
    MaterialManager* m_materialMgr = nullptr;
    std::vector<MaterialRecord> m_materialList;
    bool m_materialListLoaded = false;
    int m_selectedMaterialIdx = -1;
    bool m_materialSelected = false;

    // Heightmap cache state
    bool m_hmInitAttempted = false;   // Prevents re-triggering auto-load every frame
    bool m_hmFileMissing = false;     // Shows locate/regen/cancel UI
    bool m_heightmapSaved = false;    // Tracks whether auto-save fired
    bool m_hmRegenConfirm = false;    // Regenerate confirmation popup open
    std::string m_hmMissingPath;      // Path of the missing .dwhm file

    // Heightmap preview texture
    u32 m_hmPreviewTex = 0;
    int m_hmPreviewW = 0;
    int m_hmPreviewH = 0;
    void uploadHeightmapPreview();

    // Preview state
    bool m_toolpathGenerated = false;
    int m_settingsVersion = 0;       // Bumped when toolpath-affecting settings change
    int m_generatedAtVersion = -1;   // Version when toolpath was last generated
    f32 m_previewZoom = 1.0f;
    bool m_showFinishing = true;
    bool m_showClearing = true;
    u32 m_overlayTexture = 0;
    int m_overlayWidth = 0;
    int m_overlayHeight = 0;

    // Outline test state
    bool m_outlineCompleted = false;
    bool m_outlineSkipped = false;
    bool m_outlineRunning = false;
    int m_outlineCmdIndex = 0;

    // Zero confirm state
    bool m_zeroConfirmed = false;

    // Touch plate probe parameters
    enum class ProbeMode { ZOnly, XOnly, YOnly, XYCorner, XYZAuto };
    ProbeMode m_probeMode = ProbeMode::ZOnly;
    int m_probeCorner = 0;               // 0=BL, 1=BR, 2=TR, 3=TL
    f32 m_probeZThickness = 15.0f;       // Z plate thickness (mm)
    f32 m_probeXYThickness = 10.0f;      // XY wall thickness (mm)
    f32 m_probeFastSpeed = 150.0f;       // Fast seek speed (mm/min)
    f32 m_probeSlowSpeed = 75.0f;        // Slow/accurate speed (mm/min)
    f32 m_probeSearchDist = 30.0f;       // Max travel for probe seek (mm)
    f32 m_probeRetractDist = 2.0f;       // Retract between passes (mm)
    f32 m_probeToolDiameter = 0.0f;      // Tool diameter for XY compensation (mm)

    // Probe helpers
    void sendProbeAxis(char axis, f32 direction, f32 searchDist, f32 fastSpeed,
                       f32 slowSpeed, f32 retractDist);
    void sendProbeZ(f32 plateThickness);
    void sendProbeXY(char axis, f32 direction, f32 xyThickness, f32 toolRadius);

    // Commit state
    bool m_commitConfirmed = false;

    // Running state
    enum class RunState { Active, Paused, Completed, Aborted };
    RunState m_runState = RunState::Active;
    int m_runCurrentLine = 0;
    int m_runTotalLines = 0;
    float m_runElapsedSec = 0.0f;
    std::string m_runCurrentPass;

    // Model/tool info for summary card
    std::string m_modelName;
    std::string m_materialName;
    u32 m_modelThumbnail = 0;  // GL texture ID from library panel

    // Project directory support
    ProjectManager* m_projectManager = nullptr;
    CutOptimizerPanel* m_cutOptimizer = nullptr;
    std::function<void()> m_openToolBrowser;
    FitParamsCallback m_onFitParamsChanged;
    MaterialPartSyncCallback m_onMaterialPartSync;
    Path m_modelSourcePath;
    MachineProfileDialog m_profileDialog;

    // Project-aware save helpers
    void saveHeightmapToProject();
    void saveImageToProject();
    void saveGCodeToProject();
    void syncSetupToOptimizerAndProject();
    std::optional<i64> syncOperationOpenItem();
    std::optional<i64> selectedMaterialId() const;
    std::string selectedMaterialName() const;

    // G-code export (fallback FileDialog)
    void showExportDialog();

    // Long-press abort tracking
    float m_abortHoldTime = 0.0f;
    bool m_abortHolding = false;

    // Helper: format time as "Xm Ys"
    static std::string formatTime(f32 seconds);
};

} // namespace dw

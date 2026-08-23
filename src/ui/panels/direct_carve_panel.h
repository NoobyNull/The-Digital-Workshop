#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "panel.h"

#include "core/carve/model_fitter.h"
#include "core/carve/direct_carve_operation_state.h"
#include "core/carve/direct_carve_probe_tool_diameter.h"
#include "core/carve/direct_carve_tool_plan.h"
#include "core/carve/direct_carve_zeroing_probe.h"
#include "core/carve/direct_carve_workflow.h"
#include "core/carve/toolpath_types.h"
#include "core/cnc/machine_units.h"
#include "core/cnc/cnc_tool.h"
#include "core/cnc/cnc_types.h"
#include "core/materials/material.h"
#include "core/mesh/vertex.h"
#include "core/types.h"
#include "modules/carve_preparation/prepare_carve_flow.h"
#include "modules/carve_preparation/preparation_step_guidance.h"
#include "modules/run_coordination/run_coordinator.h"

namespace dw {

#ifdef DW_ENABLE_UX_CAPTURE
enum class DirectCarveUxCaptureState {
    DesignAndSize,
    MaterialAndBlank,
    ChooseTool,
    CarvePreview,
    ReviewMissing,
    ReviewReady,
    Streaming,
    Paused,
};

struct DirectCarveUxCaptureSnapshot {
    std::string stage;
    std::string design;
    bool previewReady = false;
    bool startAvailable = false;
    run_coordination::RunState runState = run_coordination::RunState::Idle;
    bool focusPrimed = false;
    bool abortFocused = false;
};
#endif

struct DirectCarveProjectPlanSnapshot {
    carve_preparation::PrepareCarvePin pin;
    carve::DirectCarveWorkflowState workflow;
    bool blankSpecified = false;
};

class CncController;
class FileDialog;
class GCodeRepository;
class GCodePanel;
class LibraryManager;
class MaterialManager;
class ProjectDirectory;
class ProjectManager;
class ToolDatabase;
class ToolboxRepository;

namespace carve {
class CarveJob;
}

namespace gcode {
struct PreparedDocument;
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
    using ProjectDirectoryCallback =
        std::function<void(std::shared_ptr<ProjectDirectory>)>;
    using ProjectDirectoryRequest =
        std::function<void(carve_preparation::PrepareCarvePin,
                           ProjectDirectoryCallback)>;
    void setProjectDirectoryRequest(ProjectDirectoryRequest request) {
        m_projectDirectoryRequest = std::move(request);
    }
    void setOpenToolBrowserCallback(std::function<void()> cb);
    void setOpenMachineProfilesCallback(std::function<void()> cb);
    void setOnPreparationDirty(std::function<void(bool)> cb) {
        m_onPreparationDirty = std::move(cb);
    }
    void setCreateProjectRequiredCallback(std::function<void()> cb) {
        m_onCreateProjectRequested = std::move(cb);
    }
    void setCutOptimizerPanel(class CutOptimizerPanel* cop);
    using RunEffectExecutor =
        std::function<bool(const run_coordination::RunEffect&)>;
    void setRunEffectExecutor(RunEffectExecutor executor) {
        m_runEffectExecutor = std::move(executor);
    }
    void onRunProgress(const StreamProgress& progress);
    void onRunFailure(run_coordination::RunFailure failure);

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

    // An optional, presentation-only route for the exact generated G-code.
    // It never loads the sender and therefore cannot bypass protected runs.
    using GCode3DPreviewCallback =
        std::function<void(const gcode::PreparedDocument&)>;
    void setOnGCode3DPreview(GCode3DPreviewCallback cb) {
        m_onGCode3DPreview = std::move(cb);
    }
    void setOnGCode3DPreviewCleared(std::function<void()> cb) {
        m_onGCode3DPreviewCleared = std::move(cb);
    }
    void setOpen3DPreviewCallback(std::function<void()> cb) {
        m_open3DPreview = std::move(cb);
    }


    // Callbacks
    void onConnectionChanged(bool connected);
    void onStatusUpdate(const MachineStatus& status);
    void onRawLine(const std::string& line, bool isSent);
    void onModelLoaded(const std::vector<Vertex>& vertices,
                       const std::vector<u32>& indices,
                       const Vec3& boundsMin,
                       const Vec3& boundsMax,
                       const std::string& modelName = "",
                       const Path& modelSourcePath = "",
                       u32 thumbnailTexture = 0,
                       bool notifyFitPreview = true);
    bool loadOperationOpenItem(const ProjectOpenItem& item,
                               carve_preparation::PrepareCarvePin pin);
    [[nodiscard]] bool preparationDirty() const noexcept { return m_preparationDirty; }
    [[nodiscard]] const std::optional<carve_preparation::PrepareCarvePin>&
    preparationPin() const noexcept {
        return m_preparationPin;
    }
    [[nodiscard]] std::optional<DirectCarveProjectPlanSnapshot>
    projectPlanSnapshot() const;
    bool savePreparation();
    [[nodiscard]] bool hasActiveMachineAction() const noexcept;
    [[nodiscard]] bool hasActiveProtectedRun() const noexcept;

#ifdef DW_ENABLE_UX_CAPTURE
    bool stageUxCaptureState(DirectCarveUxCaptureState state);
    void primeUxCaptureAbortFocus();
    [[nodiscard]] DirectCarveUxCaptureSnapshot uxCaptureSnapshot() const;
#endif

    // Drop every value owned by the active project while retaining application
    // dependencies, callbacks, panel visibility, and live machine status.
    // Safe to call repeatedly when closing or replacing the active project.
    void clearProjectContext();

  private:
    enum class Step {
        // --- Planning (no machine required) ---
        ModelFit,      // Scale, position, depth adjustment
        MaterialSetup, // Material selection, feeds confirmation
        ToolSelect,    // Review/accept tool recommendations
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
    void applyOperationSetup(const carve::DirectCarveOperationSetup& setup);
    void syncToolpathRapidRateFromProfile();
    void applyMachineToolpathDefaults(bool updateAppliedKey = true);
    void applyMaterialToolpathRecommendation(const MaterialRecord& material);
    std::optional<i64> syncZeroingOpenItem();
    carve::DirectCarveZeroingSetup currentZeroingSetup() const;
    carve::DirectCarveZeroProbeMode currentZeroProbeMode() const;
    carve::DirectCarveAutoZeroBitMode currentAutoZeroBitMode() const;
    carve::DirectCarveZeroCorner currentZeroCorner() const;

    // Navigation
    void renderStepIndicator();
    void renderNavButtons();
    bool canAdvance();
    void navigateToStep(Step target);
    void advanceStep();
    void retreatStep();
    carve::DirectCarveWorkflowState workflowState() const;
    carve::DirectCarveWorkflowStep workflowStep(Step step) const;
    bool isStepSatisfied(Step step) const;
    bool canStartCarve() const;
    bool hasCurrentToolpath() const;
    void clearFinalConfirmation();
    void markToolpathSettingsChanged();
    void markToolPlanChanged();
    void markGeometryChanged();
    [[nodiscard]] const gcode::PreparedDocument*
    ensureCurrentGCodeDocument();
    void publishGCode3DPreview();
    void clearGCode3DPreview();
    void setPreparationDirty(bool dirty);
    void renderPreparationContext();
    [[nodiscard]] std::optional<carve_preparation::PreparationReadinessSnapshot>
    preparationReadinessSnapshot() const;
    void beginPinnedPreparation();
    void refreshPinnedPreparation();
    [[nodiscard]] bool requestPinnedPreviewGeneration();
    void completePinnedPreviewGeneration(bool generated);
    [[nodiscard]] bool pinnedPreparationActive() const noexcept;
    [[nodiscard]] static std::optional<carve_preparation::PreparationStageId>
    preparationStage(Step step) noexcept;
    [[nodiscard]] static Step
    preparationStep(carve_preparation::PreparationStageId stage) noexcept;
    bool renderPreparationStepGuidance(
        carve_preparation::PreparationStageId stage);
    void requestRunStart();
    void requestRunPause();
    void requestRunResume();
    void requestRunAbort();
    bool applyRunTransition(const run_coordination::RunTransition& transition);
    void failActiveRun(run_coordination::RunFailure failure);
    [[nodiscard]] run_coordination::RunPreflightFacts runPreflightFacts() const;

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
    bool m_homingVerified = false;
    bool m_homingSkipped = false;
    bool m_stockSecuredConfirmed = false;

    // Model data (set via onModelLoaded)
    bool m_modelLoaded = false;
    std::vector<Vertex> m_modelVertices;
    std::vector<u32> m_modelIndices;
    Vec3 m_modelBoundsMin{0.0f};
    Vec3 m_modelBoundsMax{0.0f};

    // Per-step state (populated as wizard progresses)
    carve::FitParams m_fitParams;
    carve::ToolpathConfig m_toolpathConfig;
    carve::StockDimensions m_stock;
    carve::ModelFitter m_fitter;
    std::optional<carve::DirectCarveOperationSetup> m_pendingOperationSetup;

    // Tool selection state
    carve::DirectCarveToolPlan m_toolPlan;
    carve::DirectCarveToolPickerRole m_toolPickerRole =
        carve::DirectCarveToolPickerRole::Finishing;
    std::string m_toolSelectionMessage;
    bool m_toolSetupConfirmed = false;
    bool m_toolLibraryLoaded = false;
    std::vector<VtdbToolGeometry> m_libraryTools;  // Currently displayed tool list
    std::vector<VtdbToolGeometry> m_toolboxTools;  // My Toolbox subset
    std::vector<VtdbToolGeometry> m_allTools;      // Full library
    bool m_showAllTools = false;                   // false = My Toolbox, true = All Tools
    bool m_useManualTool = false;
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
    bool m_machineToolpathDefaultsApplied = false;
    std::string m_machineToolpathDefaultsKey;

    // Preview state
    bool m_toolpathGenerated = false;
    int m_settingsVersion = 0;       // Bumped when toolpath-affecting settings change
    int m_generatedAtVersion = -1;   // Version when toolpath was last generated
    f32 m_previewZoom = 1.0f;
    bool m_showClearing = true;
    bool m_showFinishing = true;
    std::string m_autoRoughingWarning;
    bool m_surfaceToolpathPending = false;
    int m_surfaceToolpathPendingVersion = -1;
    std::shared_ptr<const gcode::PreparedDocument> m_preparedGCode;
    int m_preparedGCodeVersion = -1;
    std::optional<cnc::SendUnits> m_preparedGCodeUnits;
    std::string m_preparedGCodeProfileKey;
    bool m_gcode3DPreviewPublished = false;

    // Outline test state
    bool m_outlineCompleted = false;
    bool m_outlineSkipped = false;
    bool m_outlineRunning = false;
    int m_outlineCmdIndex = 0;

    // Zero confirm state
    bool m_zeroConfirmed = false;
    carve::DirectCarveTouchPlate m_touchPlate =
        carve::DirectCarveTouchPlate::Generic;
    carve::DirectCarveAutoZeroBitMode m_autoZeroBitMode =
        carve::DirectCarveAutoZeroBitMode::Auto;
    bool m_autoZeroBitModeManual = false;

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
    carve::DirectCarveProbeToolDiameter m_probeToolDiameter;
    f32 m_autoZeroOriginOffset = 22.5f;  // Center-to-work-origin offset
    f32 m_autoZeroFinalZRetract = 1.0f;  // Final retract after AutoZero Z set

    // Probe helpers
    void sendProbeAxis(char axis, f32 direction, f32 searchDist, f32 fastSpeed,
                       f32 slowSpeed, f32 retractDist);
    void sendProbeZ(f32 plateThickness);
    void sendProbeXY(char axis, f32 direction, f32 xyThickness, f32 toolRadius);
    void startSienciAutoZeroProbe();
    void sendNextZeroingStep();
    void finishZeroingRun(bool success, const std::string& message);

    enum class ZeroingStepKind {
        Command,
        ProbeXFirst,
        ProbeXSecond,
        MoveToXCenter,
        SetXOffset,
        ProbeYFirst,
        ProbeYSecond,
        MoveToYCenter,
        SetYOffset,
    };

    struct ZeroingStep {
        ZeroingStepKind kind = ZeroingStepKind::Command;
        std::string command;
    };

    std::vector<ZeroingStep> m_zeroingSteps;
    size_t m_zeroingStepIndex = 0;
    ZeroingStepKind m_zeroingPendingProbeStep = ZeroingStepKind::Command;
    bool m_zeroingRunActive = false;
    bool m_zeroingWaitingForOk = false;
    bool m_zeroingSawProbeResult = false;
    std::optional<carve::DirectCarveProbeResult> m_zeroingLastProbeResult;
    f32 m_autoZeroXFirst = 0.0f;
    f32 m_autoZeroXSecond = 0.0f;
    f32 m_autoZeroYFirst = 0.0f;
    f32 m_autoZeroYSecond = 0.0f;
    std::string m_zeroingRunMessage;

    // Commit state
    bool m_commitConfirmed = false;
    int m_commitConfirmedSettingsVersion = -1;
    int m_commitConfirmedToolpathVersion = -1;

    // Protected Run state. Machine effects leave this panel through one typed
    // executor; PrepareCarveFlow cannot express any of them.
    run_coordination::RunCoordinator m_runCoordinator;
    RunEffectExecutor m_runEffectExecutor;
    std::uint64_t m_nextRunId = 1;
    std::uint64_t m_runEventSequence = 0;
    std::uint64_t m_preflightRevision = 0;
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
    ProjectDirectoryRequest m_projectDirectoryRequest;
    std::optional<carve_preparation::PrepareCarvePin> m_preparationPin;
    carve_preparation::PrepareCarveFlow m_preparationFlow;
    std::uint64_t m_preparationEditRevision = 0;
    int m_lastPreparationSettingsVersion = -1;
    bool m_preparationDirty = false;
    bool m_restoringOperationSetup = false;
    std::function<void(bool)> m_onPreparationDirty;
    std::function<void()> m_onCreateProjectRequested;
    CutOptimizerPanel* m_cutOptimizer = nullptr;
    std::function<void()> m_openToolBrowser;
    std::function<void()> m_openMachineProfiles;
    std::function<void()> m_open3DPreview;
    FitParamsCallback m_onFitParamsChanged;
    GCode3DPreviewCallback m_onGCode3DPreview;
    std::function<void()> m_onGCode3DPreviewCleared;
    MaterialPartSyncCallback m_onMaterialPartSync;
    Path m_modelSourcePath;
    int m_maxStepVisited = 0;

    struct SavedRunToolpath {
        workshop::ProjectItemRef gcodeItem;
        std::uint64_t editRevision = 0;
        std::string fingerprint;
        Path filePath;
    };
    std::optional<SavedRunToolpath> m_savedRunToolpath;

    // Project-aware save helpers
    void saveGCodeToProject();
    void saveGCodeToProjectDirectory(carve_preparation::PrepareCarvePin pin,
                                     std::shared_ptr<ProjectDirectory> directory,
                                     std::function<void(bool)> completion = {});
    void syncSetupToOptimizerAndProject();
    cnc::SendUnits detectedSendUnits() const;
    std::optional<i64> syncOperationOpenItem();
    std::optional<i64> persistOperationOpenItem();
    std::optional<ProjectOpenItem> pinnedOperationOpenItem() const;
    bool reconcileToolOpenItems();
    std::optional<i64> syncToolOpenItem(const std::string& role,
                                        const VtdbToolGeometry& tool);
    bool removeToolOpenItems(const std::string& role);
    std::optional<i64> selectedMaterialId() const;
    std::string selectedMaterialName() const;

    // G-code export (fallback FileDialog)
    void showExportDialog();

    // Long-press abort tracking
    float m_abortHoldTime = 0.0f;
    bool m_abortHolding = false;

#ifdef DW_ENABLE_UX_CAPTURE
    bool m_uxCapturePrimeAbortFocus = false;
    bool m_uxCaptureFocusPrimed = false;
    bool m_uxCaptureAbortFocused = false;
#endif

    // Helper: format time as "Xm Ys"
    static std::string formatTime(f32 seconds);
};

} // namespace dw

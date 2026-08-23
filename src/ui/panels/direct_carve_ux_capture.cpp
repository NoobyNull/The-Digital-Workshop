#include "ui/panels/direct_carve_panel.h"

#include <algorithm>
#include <cstdio>
#include <variant>

#include "core/carve/carve_job.h"
#include "core/cnc/cnc_controller.h"
#include "core/materials/material_manager.h"

namespace dw {
namespace {

VtdbToolGeometry captureTool() {
    VtdbToolGeometry tool;
    tool.id = "ux-capture-ball-nose-3.175";
    tool.name_format = "3.175 mm Ball Nose";
    tool.tool_type = VtdbToolType::BallNose;
    tool.units = VtdbUnits::Metric;
    tool.diameter = 3.175;
    tool.tip_radius = 1.5875;
    tool.num_flutes = 2;
    return tool;
}

} // namespace

bool DirectCarvePanel::stageUxCaptureState(DirectCarveUxCaptureState state) {
    using carve_preparation::ContinuePreparation;
    using carve_preparation::PreparationReady;
    using carve_preparation::PreparationStageId;
    using carve_preparation::PreparationStageState;
    using carve_preparation::PrepareCarveTransitionStatus;

    auto reject = [this](const char* reason) {
        const auto& snapshot = m_preparationFlow.snapshot();
        std::fprintf(stderr,
                     "DW_UX_CAPTURE_STAGE_ERROR=%s stage=%d states=%d,%d,%d,%d "
                     "material_count=%zu material=%d tool=%d setup=%d preview=%d "
                     "stock=%.3f,%.3f,%.3f fit=%.3f,%.3f bounds=%.3f,%.3f,%.3f\n",
                     reason,
                     static_cast<int>(snapshot.activeStage),
                     static_cast<int>(snapshot.stages[0].state),
                     static_cast<int>(snapshot.stages[1].state),
                     static_cast<int>(snapshot.stages[2].state),
                     static_cast<int>(snapshot.stages[3].state),
                     m_materialList.size(),
                     m_materialSelected,
                     m_toolPlan.finishingIntent().has_value(),
                     m_toolSetupConfirmed,
                     m_toolpathGenerated,
                     static_cast<double>(m_stock.width),
                     static_cast<double>(m_stock.height),
                     static_cast<double>(m_stock.thickness),
                     static_cast<double>(m_fitParams.scale),
                     static_cast<double>(m_fitParams.depthMm),
                     static_cast<double>(m_modelBoundsMax.x - m_modelBoundsMin.x),
                     static_cast<double>(m_modelBoundsMax.y - m_modelBoundsMin.y),
                     static_cast<double>(m_modelBoundsMax.z - m_modelBoundsMin.z));
        return false;
    };

    if (!m_modelLoaded || !m_carveJob || !m_preparationPin ||
        !pinnedPreparationActive()) {
        return reject("preparation-not-active");
    }
    m_uxCapturePrimeAbortFocus = false;
    m_uxCaptureFocusPrimed = false;
    m_uxCaptureAbortFocused = false;

    auto ensureDesign = [this]() {
        const float extentX = m_modelBoundsMax.x - m_modelBoundsMin.x;
        const float extentY = m_modelBoundsMax.y - m_modelBoundsMin.y;
        const float extentZ = m_modelBoundsMax.z - m_modelBoundsMin.z;
        if (extentX <= 0.0f || extentY <= 0.0f || extentZ <= 0.0f)
            return false;
        constexpr float margin = 5.0f;
        m_stock = {extentX + margin * 2.0f,
                   extentY + margin * 2.0f,
                   std::max(12.0f, extentZ + 3.0f)};
        m_fitParams.scale = 1.0f;
        m_fitParams.depthMm = std::max(1.0f, std::min(2.0f, extentZ));
        m_fitParams.offsetX = margin;
        m_fitParams.offsetY = margin;
        m_fitter.setStock(m_stock);
        refreshPinnedPreparation();
        return m_preparationFlow.snapshot()
                   .stage(carve_preparation::PreparationStageId::DesignAndSize)
                   .state == carve_preparation::PreparationStageState::Complete;
    };
    auto ensureMaterial = [this]() {
        if (!m_materialListLoaded && m_materialMgr) {
            m_materialList = m_materialMgr->getAllMaterials();
            m_materialListLoaded = true;
        }
        if (m_materialList.empty()) return false;
        m_selectedMaterialIdx = 0;
        m_materialName = m_materialList.front().name;
        m_materialSelected = true;
        refreshPinnedPreparation();
        return m_preparationFlow.snapshot()
                   .stage(carve_preparation::PreparationStageId::MaterialAndBlank)
                   .state == carve_preparation::PreparationStageState::Complete;
    };
    auto ensureTool = [this]() {
        const auto selected = m_toolPlan.selectTool(
            carve::DirectCarveToolPickerRole::Finishing, captureTool());
        if (!selected) return false;
        m_toolSetupConfirmed = true;
        refreshPinnedPreparation();
        return m_preparationFlow.snapshot()
                   .stage(carve_preparation::PreparationStageId::ChooseTool)
                   .state == carve_preparation::PreparationStageState::Complete;
    };
    auto advance = [this](PreparationStageId expected) {
        if (m_preparationFlow.snapshot().activeStage != expected) return false;
        const auto transition =
            m_preparationFlow.dispatch(ContinuePreparation{});
        if (transition.status != PrepareCarveTransitionStatus::Applied) return false;
        m_currentStep = preparationStep(transition.snapshot.activeStage);
        m_maxStepVisited =
            std::max(m_maxStepVisited, static_cast<int>(m_currentStep));
        return true;
    };

    if (state == DirectCarveUxCaptureState::DesignAndSize) {
        m_currentStep = Step::ModelFit;
        return m_preparationFlow.snapshot().activeStage ==
               PreparationStageId::DesignAndSize;
    }
    if (!ensureDesign()) return reject("design-not-ready");
    if (!advance(PreparationStageId::DesignAndSize))
        return reject("design-did-not-advance");
    if (state == DirectCarveUxCaptureState::MaterialAndBlank) return true;

    if (!ensureMaterial() ||
        !advance(PreparationStageId::MaterialAndBlank)) {
        return reject("material-did-not-advance");
    }
    if (state == DirectCarveUxCaptureState::ChooseTool) return true;

    if (!ensureTool() || !advance(PreparationStageId::ChooseTool))
        return reject("tool-did-not-advance");
    if (state == DirectCarveUxCaptureState::CarvePreview) {
        // Continue below so the visible preview contains deterministic output.
    }

    if (!requestPinnedPreviewGeneration())
        return reject("preview-request-not-issued");
    m_toolpathConfig.cutExtents = carve::CutExtents::Model;
    m_toolpathConfig.stepoverPreset = carve::StepoverPreset::Roughing;
    m_toolpathConfig.customStepoverPct = 40.0f;
    m_toolpathConfig.scanResolutionMm = 2.0f;
    m_toolpathConfig.feedRateMmMin = 1000.0f;
    m_toolpathConfig.plungeRateMmMin = 300.0f;
    const float margin = 2.0f;
    const Vec3 stockMin{0.0f, 0.0f, -m_stock.thickness};
    const Vec3 stockMax{m_stock.width, m_stock.height, 0.0f};
    const Vec3 modelMin{margin, margin, -m_fitParams.depthMm};
    const Vec3 modelMax{
        std::max(margin + 1.0f, m_stock.width - margin),
        std::max(margin + 1.0f, m_stock.height - margin),
        0.0f};
    const auto& finishingTool = m_toolPlan.finishingIntent();
    if (!finishingTool) return reject("finishing-tool-missing");
    m_carveJob->generateFixedDepthToolpath(
        stockMin, stockMax, modelMin, modelMax, m_fitParams.depthMm,
        m_toolpathConfig, *finishingTool);
    m_toolpathGenerated =
        m_carveJob->state() == carve::CarveJobState::Ready;
    m_generatedAtVersion = m_settingsVersion;
    completePinnedPreviewGeneration(m_toolpathGenerated);
    if (m_toolpathGenerated) publishGCode3DPreview();
    if (!hasCurrentToolpath() ||
        m_preparationFlow.snapshot().stage(PreparationStageId::CarvePreview).state !=
            PreparationStageState::Complete) {
        return reject("preview-not-complete");
    }
    m_currentStep = Step::Preview;
    if (state == DirectCarveUxCaptureState::CarvePreview) return true;

    const auto handoff = m_preparationFlow.dispatch(ContinuePreparation{});
    if (handoff.status != PrepareCarveTransitionStatus::EffectIssued ||
        !handoff.effect || !std::holds_alternative<PreparationReady>(*handoff.effect)) {
        return reject("preparation-handoff-not-issued");
    }

    m_cncConnected = m_cnc && m_cnc->isConnected();
    m_machineStatus.state = MachineState::Idle;
    m_homingVerified = true;
    m_homingSkipped = false;
    m_safeZConfirmed = true;
    m_zeroConfirmed = true;
    m_outlineCompleted = true;
    m_outlineSkipped = false;
    m_stockSecuredConfirmed =
        state != DirectCarveUxCaptureState::ReviewMissing;
    m_commitConfirmed = m_stockSecuredConfirmed;
    m_commitConfirmedSettingsVersion = m_settingsVersion;
    m_commitConfirmedToolpathVersion = m_generatedAtVersion;
    m_currentStep = Step::Commit;
    m_maxStepVisited = static_cast<int>(Step::Commit);

    if (state == DirectCarveUxCaptureState::ReviewMissing)
        return !canStartCarve();
    if (!canStartCarve()) return reject("review-not-ready");
    if (state == DirectCarveUxCaptureState::ReviewReady) return true;

    requestRunStart();
    if (m_runCoordinator.snapshot().state !=
        run_coordination::RunState::Streaming) {
        return reject("run-not-streaming");
    }
    if (state == DirectCarveUxCaptureState::Streaming) return true;

    requestRunPause();
    return m_runCoordinator.snapshot().state ==
           run_coordination::RunState::Paused;
}

void DirectCarvePanel::primeUxCaptureAbortFocus() {
    m_uxCapturePrimeAbortFocus = true;
    m_uxCaptureFocusPrimed = false;
    m_uxCaptureAbortFocused = false;
}

DirectCarveUxCaptureSnapshot DirectCarvePanel::uxCaptureSnapshot() const {
    const char* stage = "Unknown";
    switch (m_currentStep) {
    case Step::ModelFit: stage = "Design & Size"; break;
    case Step::MaterialSetup: stage = "Material & Blank"; break;
    case Step::ToolSelect: stage = "Choose Tool"; break;
    case Step::Preview: stage = "Carve Preview"; break;
    case Step::MachineCheck:
    case Step::ZeroConfirm:
    case Step::OutlineTest: stage = "Machine Setup"; break;
    case Step::Commit: stage = "Review & Run"; break;
    case Step::Running: stage = "Run CNC"; break;
    }
    return {
        stage,
        m_modelName,
        hasCurrentToolpath(),
        canStartCarve(),
        m_runCoordinator.snapshot().state,
        m_uxCaptureFocusPrimed,
        m_uxCaptureAbortFocused,
    };
}

} // namespace dw

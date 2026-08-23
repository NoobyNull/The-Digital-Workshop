// Project planning persistence for the Direct Carve workflow.

#include "ui/panels/direct_carve_panel.h"

#include <cstdio>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/carve/carve_job.h"
#include "core/config/config.h"
#include "core/gcode/machine_profile.h"
#include "core/project/project.h"
#include "core/project/project_directory.h"
#include "ui/panels/cut_optimizer_panel.h"
#include "ui/panels/direct_carve_tool_child_sync.h"

namespace dw {
namespace {

const char* scanAxisLabel(carve::ScanAxis axis) {
    switch (axis) {
    case carve::ScanAxis::XOnly:
        return "x_only";
    case carve::ScanAxis::YOnly:
        return "y_only";
    case carve::ScanAxis::XThenY:
        return "x_then_y";
    case carve::ScanAxis::YThenX:
        return "y_then_x";
    }
    return "unknown";
}

const char* cutExtentsLabel(carve::CutExtents extents) {
    switch (extents) {
    case carve::CutExtents::Model:
        return "model";
    case carve::CutExtents::Material:
        return "material";
    }
    return "unknown";
}

const char* millDirectionLabel(carve::MillDirection direction) {
    switch (direction) {
    case carve::MillDirection::Climb:
        return "climb";
    case carve::MillDirection::Conventional:
        return "conventional";
    case carve::MillDirection::Alternating:
        return "alternating";
    }
    return "unknown";
}

const char* stepoverPresetLabel(carve::StepoverPreset preset) {
    switch (preset) {
    case carve::StepoverPreset::UltraFine:
        return "ultra_fine";
    case carve::StepoverPreset::Fine:
        return "fine";
    case carve::StepoverPreset::Basic:
        return "basic";
    case carve::StepoverPreset::Rough:
        return "rough";
    case carve::StepoverPreset::Roughing:
        return "roughing";
    }
    return "unknown";
}

std::string formatStockDimensions(const carve::StockDimensions& stock) {
    char buf[96];
    std::snprintf(buf,
                  sizeof(buf),
                  "%.0fx%.0fx%.0fmm",
                  static_cast<double>(stock.width),
                  static_cast<double>(stock.height),
                  static_cast<double>(stock.thickness));
    return buf;
}

} // anonymous namespace

bool DirectCarvePanel::hasActiveMachineAction() const noexcept {
    const bool runActive = m_runCoordinator.snapshot().live();
    return m_outlineRunning || m_zeroingRunActive || runActive;
}

void DirectCarvePanel::clearProjectContext() {
    clearGCode3DPreview();
    if (m_carveJob && m_carveJob->state() == carve::CarveJobState::Computing) {
        m_carveJob->cancel();
    }

    if (m_preparationPin && m_preparationFlow.snapshot().active) {
        (void)m_preparationFlow.dispatch(carve_preparation::EndPreparation{*m_preparationPin});
    }
    setPreparationDirty(false);
    m_preparationPin.reset();
    m_preparationEditRevision = 0;
    m_lastPreparationSettingsVersion = -1;
    m_restoringOperationSetup = false;

    m_title = "Direct Carve";
    m_currentStep = Step::ModelFit;
    m_maxStepVisited = 0;

    m_safeZConfirmed = false;
    m_homingVerified = false;
    m_homingSkipped = false;
    m_stockSecuredConfirmed = false;

    m_modelLoaded = false;
    m_modelVertices.clear();
    m_modelIndices.clear();
    m_modelBoundsMin = Vec3{0.0f};
    m_modelBoundsMax = Vec3{0.0f};
    m_modelName.clear();
    m_modelSourcePath.clear();
    m_modelThumbnail = 0;

    m_fitParams = carve::FitParams{};
    m_toolpathConfig = carve::ToolpathConfig{};
    m_stock = carve::StockDimensions{};
    m_fitter = carve::ModelFitter{};
    m_pendingOperationSetup.reset();

    m_toolPlan = carve::DirectCarveToolPlan{};
    m_toolPickerRole = carve::DirectCarveToolPickerRole::Finishing;
    m_toolSelectionMessage.clear();
    m_toolSetupConfirmed = false;
    m_toolLibraryLoaded = false;
    m_libraryTools.clear();
    m_toolboxTools.clear();
    m_allTools.clear();
    m_showAllTools = false;
    m_useManualTool = false;
    m_manualToolType = 0;
    m_manualDiameter = 3.175f;
    m_manualAngle = 90.0f;
    m_manualTipRadius = 1.5875f;
    m_manualFlutes = 2;

    m_materialList.clear();
    m_materialListLoaded = false;
    m_selectedMaterialIdx = -1;
    m_materialSelected = false;
    m_materialName.clear();
    m_machineToolpathDefaultsApplied = false;
    m_machineToolpathDefaultsKey.clear();

    m_toolpathGenerated = false;
    m_settingsVersion = 0;
    m_generatedAtVersion = -1;
    m_previewZoom = 1.0f;
    m_showClearing = true;
    m_showFinishing = true;
    m_autoRoughingWarning.clear();
    m_surfaceToolpathPending = false;
    m_surfaceToolpathPendingVersion = -1;

    m_outlineCompleted = false;
    m_outlineSkipped = false;
    m_outlineRunning = false;
    m_outlineCmdIndex = 0;

    m_zeroConfirmed = false;
    m_touchPlate = carve::DirectCarveTouchPlate::Generic;
    m_autoZeroBitMode = carve::DirectCarveAutoZeroBitMode::Auto;
    m_autoZeroBitModeManual = false;
    m_probeMode = ProbeMode::ZOnly;
    m_probeCorner = 0;
    m_probeZThickness = 15.0f;
    m_probeXYThickness = 10.0f;
    m_probeFastSpeed = 150.0f;
    m_probeSlowSpeed = 75.0f;
    m_probeSearchDist = 30.0f;
    m_probeRetractDist = 2.0f;
    m_probeToolDiameter.reset();
    m_autoZeroOriginOffset = 22.5f;
    m_autoZeroFinalZRetract = 1.0f;
    m_zeroingSteps.clear();
    m_zeroingStepIndex = 0;
    m_zeroingPendingProbeStep = ZeroingStepKind::Command;
    m_zeroingRunActive = false;
    m_zeroingWaitingForOk = false;
    m_zeroingSawProbeResult = false;
    m_zeroingLastProbeResult.reset();
    m_autoZeroXFirst = 0.0f;
    m_autoZeroXSecond = 0.0f;
    m_autoZeroYFirst = 0.0f;
    m_autoZeroYSecond = 0.0f;
    m_zeroingRunMessage.clear();

    clearFinalConfirmation();
    m_runCoordinator = run_coordination::RunCoordinator{};
    m_runEventSequence = 0;
    m_runCurrentLine = 0;
    m_runTotalLines = 0;
    m_runElapsedSec = 0.0f;
    m_runCurrentPass.clear();
    m_savedRunToolpath.reset();
    m_abortHoldTime = 0.0f;
    m_abortHolding = false;
}

void DirectCarvePanel::syncSetupToOptimizerAndProject() {
    if (!m_modelLoaded) {
        return;
    }

    const auto partName = m_modelName.empty() ? std::string("Carve blank") : m_modelName;
    const auto materialId = selectedMaterialId();
    const auto materialName = selectedMaterialName();

    if (m_cutOptimizer) {
        optimizer::Part part;
        part.name = partName;
        part.width = m_stock.width;
        part.height = m_stock.height;
        part.quantity = 1;
        part.canRotate = true;
        part.materialId = materialId.value_or(0);
        m_cutOptimizer->upsertPart(part);
    }

    const auto operation = pinnedOperationOpenItem();
    if (!operation || !m_preparationPin || !m_projectDirectoryRequest) {
        return;
    }

    MaterialPartSync data;
    data.key = "[auto:direct-carve:" + ProjectDirectory::sanitizeName(partName) + "]";
    data.name = partName + " blank";
    data.materialName = materialName.empty() ? "Direct Carve Material" : materialName;
    data.dimensions = formatStockDimensions(m_stock);
    data.quantity = 1.0;
    data.unit = "blank";

    if (m_cutOptimizer) {
        auto stockSelection = m_cutOptimizer->currentStockSelection();
        if (stockSelection) {
            if (stockSelection->stockSize) {
                const auto& stock = *stockSelection->stockSize;
                data.stockSizeDbId = stock.id;
                const f64 sheetArea = stock.widthMm * stock.heightMm;
                const f64 blankArea = static_cast<f64>(m_stock.width) *
                                      static_cast<f64>(m_stock.height);
                if (sheetArea > 0.0 && stock.pricePerUnit > 0.0) {
                    data.unitRate = stock.pricePerUnit * (blankArea / sheetArea);
                }
                if (data.materialName == "Direct Carve Material" && stockSelection->material) {
                    data.materialName = stockSelection->material->name;
                }
            } else if (stockSelection->sheet.area() > 0.0f && stockSelection->sheet.cost > 0.0f) {
                const f64 blankArea = static_cast<f64>(m_stock.width) *
                                      static_cast<f64>(m_stock.height);
                data.unitRate = static_cast<f64>(stockSelection->sheet.cost) *
                                (blankArea / static_cast<f64>(stockSelection->sheet.area()));
            }
        }
    }

    const auto pin = *m_preparationPin;
    m_projectDirectoryRequest(
        pin, [this, pin, data = std::move(data)](std::shared_ptr<ProjectDirectory> directory) {
            if (!directory || directory->projectId() != pin.project().value || !m_preparationPin ||
                !(*m_preparationPin == pin)) {
                return;
            }
            if (!syncOperationOpenItem()) {
                return;
            }
            if (m_onMaterialPartSync) {
                m_onMaterialPartSync(data);
            }
        });
}

std::optional<i64> DirectCarvePanel::persistOperationOpenItem() {
    auto operation = pinnedOperationOpenItem();
    if (!operation || !m_modelLoaded || !m_preparationPin) {
        return std::nullopt;
    }

    const auto partName = m_modelName.empty() ? std::string("Carve blank") : m_modelName;
    const auto materialId = selectedMaterialId();
    const auto materialName = selectedMaterialName();
    const auto& profile = Config::instance().getActiveMachineProfile();

    nlohmann::json intent = {
        {"operation_kind", "direct_carve"},
        {"description", "Generated from the Direct Carve setup wizard."},
        {"model_name", partName},
        {"model_source_path", m_modelSourcePath.string()},
        {"material_id",
         materialId.has_value() ? nlohmann::json(*materialId) : nlohmann::json(nullptr)},
        {"material_name", materialName},
        {"stock",
         {
             {"width_mm", m_stock.width},
             {"height_mm", m_stock.height},
             {"thickness_mm", m_stock.thickness},
         }},
        {"fit",
         {
             {"scale", m_fitParams.scale},
             {"offset_x_mm", m_fitParams.offsetX},
             {"offset_y_mm", m_fitParams.offsetY},
             {"depth_mm", m_fitParams.depthMm},
         }},
        {"toolpath",
         {
             {"scan_axis", scanAxisLabel(m_toolpathConfig.axis)},
             {"cut_extents", cutExtentsLabel(m_toolpathConfig.cutExtents)},
             {"mill_direction", millDirectionLabel(m_toolpathConfig.direction)},
             {"stepover_preset", stepoverPresetLabel(m_toolpathConfig.stepoverPreset)},
             {"custom_stepover_pct", m_toolpathConfig.customStepoverPct},
             {"safe_z_mm", m_toolpathConfig.safeZMm},
             {"feed_rate_mm_min", m_toolpathConfig.feedRateMmMin},
             {"plunge_rate_mm_min", m_toolpathConfig.plungeRateMmMin},
             {"rapid_rate_mm_min", m_toolpathConfig.rapidRateMmMin},
             {"stepdown_mm", m_toolpathConfig.stepdownMm},
             {"lead_in_mm", m_toolpathConfig.leadInMm},
             {"scan_resolution_mm", m_toolpathConfig.scanResolutionMm},
         }},
    };

    nlohmann::json snapshot = {
        {"machine",
         {
             {"name", profile.name},
             {"max_travel_x_mm", profile.maxTravelX},
             {"max_travel_y_mm", profile.maxTravelY},
             {"max_travel_z_mm", profile.maxTravelZ},
             {"rapid_rate_mm_min", profile.rapidRate},
             {"default_feed_rate_mm_min", profile.defaultFeedRate},
             {"default_plunge_rate_mm_min", profile.defaultPlungeRate},
             {"default_stepdown_mm", profile.defaultStepdown},
             {"spindle_max_rpm", profile.spindleMaxRPM},
             {"spindle_power_w", profile.spindlePower},
         }},
        {"setup",
         {
             {"model_loaded", m_modelLoaded},
             {"material_selected", m_materialSelected},
             {"finishing_tool_selected", m_toolPlan.finishingIntent().has_value()},
             {"toolpath_generated", m_toolpathGenerated},
             {"settings_version", m_settingsVersion},
             {"generated_at_version", m_generatedAtVersion},
         }},
        {"clearing_mode", std::string(carve::clearingToolModeKey(m_toolPlan.clearingMode()))},
    };

    if (m_toolPlan.finishingIntent()) {
        snapshot["finishing_tool"] = directCarveToolSummaryJson(*m_toolPlan.finishingIntent());
    }
    if (m_toolPlan.clearingIntent()) {
        snapshot["selected_clearing_tool"] =
            directCarveToolSummaryJson(*m_toolPlan.clearingIntent());
    }
    if (m_toolPlan.effectiveClearingTool()) {
        snapshot["effective_clearing_tool"] =
            directCarveToolSummaryJson(*m_toolPlan.effectiveClearingTool());
    }

    operation->status = (m_materialSelected && m_toolPlan.selectionComplete())
                            ? ProjectOpenItemStatus::Ready
                            : ProjectOpenItemStatus::Planned;
    operation->displayName = "Direct Carve: " + partName;
    operation->intentJson = intent.dump();
    operation->snapshotJson = snapshot.dump();

    if (!m_projectManager->updateOpenItem(*operation)) {
        return std::nullopt;
    }

    bool childrenSaved = syncZeroingOpenItem().has_value();
    childrenSaved = reconcileToolOpenItems() && childrenSaved;
    if (!childrenSaved) {
        return std::nullopt;
    }
    return operation->id;
}

std::optional<ProjectOpenItem> DirectCarvePanel::pinnedOperationOpenItem() const {
    if (!m_projectManager || !m_preparationPin) {
        return std::nullopt;
    }

    const auto& pin = *m_preparationPin;
    const auto currentProject = m_projectManager->currentProject();
    if (!currentProject || currentProject->id() != pin.project().value ||
        pin.modelItem().project != pin.project() || pin.operationItem().project != pin.project() ||
        pin.modelSource().kind != workshop::LibraryItemKind::Model || !pin.modelSource().valid() ||
        !pin.token().valid()) {
        return std::nullopt;
    }

    const auto model = m_projectManager->findOpenItem(pin.modelItem().item.value);
    if (!model || model->projectId != pin.project().value ||
        model->itemType != ProjectOpenItemType::Model || model->sourceTable != "models" ||
        !model->sourceId || *model->sourceId != pin.modelSource().item.value ||
        model->status == ProjectOpenItemStatus::Missing ||
        model->status == ProjectOpenItemStatus::Stale) {
        return std::nullopt;
    }

    const auto operation = m_projectManager->findOpenItem(pin.operationItem().item.value);
    if (!operation || operation->projectId != pin.project().value ||
        operation->itemType != ProjectOpenItemType::Operation || !operation->parentItemId ||
        *operation->parentItemId != pin.modelItem().item.value ||
        operation->status == ProjectOpenItemStatus::Missing ||
        operation->status == ProjectOpenItemStatus::Stale) {
        return std::nullopt;
    }
    return operation;
}

} // namespace dw

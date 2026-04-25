// Direct Carve wizard panel -- step-by-step guided workflow for streaming
// 2.5D toolpaths directly from STL models. Each step validates before allowing
// progression. Machine readiness is verified before any carving begins.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include <glad/gl.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/carve/analysis_overlay.h"
#include "core/carve/carve_job.h"
#include "core/carve/gcode_export.h"
#include "core/carve/tool_recommender.h"
#include "core/cnc/cnc_controller.h"
#include "core/cnc/tool_calculator.h"
#include "core/config/config.h"
#include "core/database/gcode_repository.h"
#include "core/project/project.h"
#include "core/project/project_directory.h"
#include "core/database/tool_database.h"
#include "core/database/toolbox_repository.h"
#include "core/gcode/machine_profile.h"
#include "core/library/library_manager.h"
#include "core/loaders/gcode_loader.h"
#include "core/materials/material_manager.h"
#include "core/mesh/hash.h"
#include "core/paths/path_resolver.h"
#include "core/utils/file_utils.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/icons.h"
#include "ui/panels/cut_optimizer_panel.h"
#include "ui/panels/gcode_panel.h"
#include "ui/theme.h"
#include "ui/ui_colors.h"
#include "ui/widgets/toast.h"

namespace dw {

// Aliases for concise usage within this file
static constexpr auto& kGreen = colors::kSuccess;
static constexpr auto& kRed = colors::kError;
static constexpr auto& kYellow = colors::kWarning;
static constexpr auto& kDimmed = colors::kDimmed;
static constexpr auto& kBright = colors::kInfo;

// Return tool diameter in mm regardless of stored units.
static f64 diameterMm(const VtdbToolGeometry& g) {
    return (g.units == VtdbUnits::Imperial) ? g.diameter * 25.4 : g.diameter;
}

namespace {

// ProgressBar with centered overlay text (ImGui default left-aligns within filled area)
void CenteredProgressBar(float fraction, const ImVec2& size, const char* overlay) {
    ImGui::ProgressBar(fraction, size, "");
    if (overlay && overlay[0]) {
        ImVec2 textSize = ImGui::CalcTextSize(overlay);
        ImVec2 barMin = ImGui::GetItemRectMin();
        ImVec2 barMax = ImGui::GetItemRectMax();
        float cx = barMin.x + (barMax.x - barMin.x - textSize.x) * 0.5f;
        float cy = barMin.y + (barMax.y - barMin.y - textSize.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(ImVec2(cx, cy), IM_COL32(255, 255, 255, 255), overlay);
    }
}

// Toolpath overlay colors
constexpr ImU32 kFinishColor = IM_COL32(80, 120, 255, 200);   // blue
constexpr ImU32 kClearColor = IM_COL32(255, 80, 80, 200);     // red
constexpr ImU32 kRapidColor = IM_COL32(80, 220, 80, 150);     // green

void statusBullet(bool ok, const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, ok ? kGreen : kRed);
    ImGui::BulletText("%s %s", ok ? "OK" : "FAIL", label);
    ImGui::PopStyleColor();
}

const char* scanAxisLabel(carve::ScanAxis axis) {
    switch (axis) {
    case carve::ScanAxis::XOnly:  return "x_only";
    case carve::ScanAxis::YOnly:  return "y_only";
    case carve::ScanAxis::XThenY: return "x_then_y";
    case carve::ScanAxis::YThenX: return "y_then_x";
    }
    return "unknown";
}

const char* millDirectionLabel(carve::MillDirection direction) {
    switch (direction) {
    case carve::MillDirection::Climb:        return "climb";
    case carve::MillDirection::Conventional: return "conventional";
    case carve::MillDirection::Alternating:  return "alternating";
    }
    return "unknown";
}

const char* stepoverPresetLabel(carve::StepoverPreset preset) {
    switch (preset) {
    case carve::StepoverPreset::UltraFine: return "ultra_fine";
    case carve::StepoverPreset::Fine:      return "fine";
    case carve::StepoverPreset::Basic:     return "basic";
    case carve::StepoverPreset::Rough:     return "rough";
    case carve::StepoverPreset::Roughing:  return "roughing";
    }
    return "unknown";
}

const char* toolTypeLabel(VtdbToolType type) {
    switch (type) {
    case VtdbToolType::BallNose:        return "ball_nose";
    case VtdbToolType::EndMill:         return "end_mill";
    case VtdbToolType::Radiused:        return "radiused";
    case VtdbToolType::VBit:            return "v_bit";
    case VtdbToolType::TaperedBallNose: return "tapered_ball_nose";
    case VtdbToolType::Drill:           return "drill";
    case VtdbToolType::ThreadMill:      return "thread_mill";
    case VtdbToolType::FormTool:        return "form_tool";
    case VtdbToolType::DiamondDrag:     return "diamond_drag";
    }
    return "unknown";
}

nlohmann::json toolSummaryJson(const VtdbToolGeometry& tool) {
    return {
        {"id", tool.id},
        {"name", resolveToolNameFormat(tool)},
        {"type", toolTypeLabel(tool.tool_type)},
        {"units", tool.units == VtdbUnits::Imperial ? "imperial" : "metric"},
        {"diameter_mm", diameterMm(tool)},
        {"included_angle_deg", tool.included_angle},
        {"flat_diameter", tool.flat_diameter},
        {"tip_radius", tool.tip_radius},
        {"flutes", tool.num_flutes},
    };
}

GCodeRecord makeGeneratedGCodeRecord(const Path& path,
                                     const std::string& name,
                                     const std::string& hash) {
    GCodeRecord record;
    record.hash = hash;
    record.name = name;
    record.filePath = PathResolver::makeStorable(path, PathCategory::GCode);
    record.fileSize = file::fileSize(path);

    GCodeLoader loader;
    auto loaded = loader.load(path);
    if (loaded) {
        const auto& metadata = loader.lastMetadata();
        record.boundsMin = metadata.boundsMin;
        record.boundsMax = metadata.boundsMax;
        record.totalDistance = metadata.totalDistance;
        record.estimatedTime = metadata.estimatedTime;
        record.feedRates = metadata.feedRates;
        record.toolNumbers = metadata.toolNumbers;
    }

    return record;
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

DirectCarvePanel::DirectCarvePanel() : Panel("Direct Carve") {}

DirectCarvePanel::~DirectCarvePanel() {
    if (m_hmPreviewTex != 0)
        glDeleteTextures(1, &m_hmPreviewTex);
}

void DirectCarvePanel::setCncController(CncController* cnc) { m_cnc = cnc; }
void DirectCarvePanel::setToolDatabase(ToolDatabase* db) { m_toolDb = db; }
void DirectCarvePanel::setToolboxRepository(ToolboxRepository* repo) { m_toolboxRepo = repo; }
void DirectCarvePanel::setCarveJob(carve::CarveJob* job) { m_carveJob = job; }
void DirectCarvePanel::setFileDialog(FileDialog* dlg) { m_fileDialog = dlg; }
void DirectCarvePanel::setGCodeRepository(GCodeRepository* repo) { m_gcodeRepo = repo; }
void DirectCarvePanel::setGCodePanel(GCodePanel* gcp) { m_gcodePanel = gcp; }
void DirectCarvePanel::setLibraryManager(LibraryManager* library) { m_libraryManager = library; }
void DirectCarvePanel::setMaterialManager(MaterialManager* mgr) { m_materialMgr = mgr; }
void DirectCarvePanel::setProjectManager(ProjectManager* pm) { m_projectManager = pm; }
void DirectCarvePanel::setOpenToolBrowserCallback(std::function<void()> cb) { m_openToolBrowser = std::move(cb); }
void DirectCarvePanel::setCutOptimizerPanel(CutOptimizerPanel* cop) { m_cutOptimizer = cop; }

void DirectCarvePanel::onConnectionChanged(bool connected) { m_cncConnected = connected; }
void DirectCarvePanel::onStatusUpdate(const MachineStatus& status) { m_machineStatus = status; }

void DirectCarvePanel::onModelLoaded(const std::vector<Vertex>& vertices,
                                      const std::vector<u32>& indices,
                                      const Vec3& boundsMin,
                                      const Vec3& boundsMax,
                                      const std::string& modelName,
                                      const Path& modelSourcePath,
                                      u32 thumbnailTexture) {
    m_vertices = vertices;
    m_indices = indices;
    m_modelLoaded = true;
    m_modelBoundsMin = boundsMin;
    m_modelBoundsMax = boundsMax;
    m_fitter.setModelBounds(boundsMin, boundsMax);
    if (!modelName.empty()) m_modelName = modelName;
    if (!modelSourcePath.empty()) m_modelSourcePath = modelSourcePath;
    m_modelThumbnail = thumbnailTexture;

    // Initialize stock from machine profile if not yet configured
    if (m_stock.width <= 0.0f || m_stock.height <= 0.0f) {
        const auto& prof = Config::instance().getActiveMachineProfile();
        m_stock.width = prof.maxTravelX;
        m_stock.height = prof.maxTravelY;
        m_stock.thickness = prof.maxTravelZ;
    }

    // Auto-fit model to stock
    m_fitter.setStock(m_stock);
    m_fitParams.scale = m_fitter.autoScale();
    if (m_pendingOperationSetup) {
        applyOperationSetup(*m_pendingOperationSetup);
        m_pendingOperationSetup.reset();
    }

    // Update window title with model name (###ID keeps ImGui window identity stable)
    if (!m_modelName.empty()) {
        m_title = m_modelName + "###Direct Carve";
    }

    // Fire FitParams callback with initial alignment
    if (m_onFitParamsChanged && m_stock.width > 0.0f && m_stock.height > 0.0f) {
        m_onFitParamsChanged(m_fitParams, m_modelBoundsMin, m_modelBoundsMax, m_stock);
    }

    // Reset heightmap cache state for new model
    m_hmInitAttempted = false;
    m_hmFileMissing = false;
    m_heightmapSaved = false;
    m_hmRegenConfirm = false;
    m_hmMissingPath.clear();
    if (m_hmPreviewTex != 0) { glDeleteTextures(1, &m_hmPreviewTex); m_hmPreviewTex = 0; }
}

bool DirectCarvePanel::loadOperationOpenItem(const ProjectOpenItem& item) {
    auto setup = carve::parseDirectCarveOperationSetup(item);
    if (!setup) {
        return false;
    }

    m_pendingOperationSetup = *setup;
    if (m_modelLoaded) {
        applyOperationSetup(*setup);
        m_pendingOperationSetup.reset();
    } else {
        m_modelName = setup->modelName;
        m_modelSourcePath = setup->modelSourcePath;
        m_stock = setup->stock;
        m_fitParams = setup->fit;
        m_toolpathConfig = setup->toolpath;
    }

    if (!setup->modelName.empty()) {
        m_title = setup->modelName + "###Direct Carve";
    }
    m_open = true;
    return true;
}

void DirectCarvePanel::applyOperationSetup(const carve::DirectCarveOperationSetup& setup) {
    if (!setup.modelName.empty()) {
        m_modelName = setup.modelName;
        m_title = setup.modelName + "###Direct Carve";
    }
    if (!setup.modelSourcePath.empty()) {
        m_modelSourcePath = setup.modelSourcePath;
    }

    m_stock = setup.stock;
    m_fitParams = setup.fit;
    m_toolpathConfig = setup.toolpath;
    m_fitter.setStock(m_stock);

    if (setup.materialId.has_value() || !setup.materialName.empty()) {
        if (!m_materialListLoaded && m_materialMgr) {
            m_materialListLoaded = true;
            m_materialList = m_materialMgr->getAllMaterials();
        }

        m_selectedMaterialIdx = -1;
        for (int i = 0; i < static_cast<int>(m_materialList.size()); ++i) {
            const auto& material = m_materialList[static_cast<size_t>(i)];
            if ((setup.materialId.has_value() && material.id == *setup.materialId) ||
                (!setup.materialName.empty() && material.name == setup.materialName)) {
                m_selectedMaterialIdx = i;
                m_materialName = material.name;
                break;
            }
        }
        if (m_selectedMaterialIdx < 0) {
            m_materialName = setup.materialName;
        }
        m_materialSelected = true;
    }

    if (setup.finishingTool) {
        m_finishTool = *setup.finishingTool;
        m_finishingToolSelected = true;
    }
    if (setup.clearingTool) {
        m_clearTool = *setup.clearingTool;
        m_clearToolSelected = true;
    }

    m_toolpathGenerated = false;
    ++m_settingsVersion;
    m_currentStep = m_finishingToolSelected ? Step::MaterialSetup : Step::ToolSelect;

    if (m_onFitParamsChanged && m_modelLoaded) {
        m_onFitParamsChanged(m_fitParams, m_modelBoundsMin, m_modelBoundsMax, m_stock);
    }
}

std::string DirectCarvePanel::formatTime(f32 seconds) {
    int mins = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%dm %ds", mins, secs);
    return buf;
}

std::optional<i64> DirectCarvePanel::selectedMaterialId() const {
    if (m_selectedMaterialIdx < 0 ||
        m_selectedMaterialIdx >= static_cast<int>(m_materialList.size())) {
        return std::nullopt;
    }
    return m_materialList[static_cast<size_t>(m_selectedMaterialIdx)].id;
}

std::string DirectCarvePanel::selectedMaterialName() const {
    if (m_selectedMaterialIdx < 0 ||
        m_selectedMaterialIdx >= static_cast<int>(m_materialList.size())) {
        return m_materialName;
    }
    return m_materialList[static_cast<size_t>(m_selectedMaterialIdx)].name;
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

    std::shared_ptr<ProjectDirectory> dir;
    if (m_projectManager) {
        dir = m_projectManager->ensureProjectForModel(m_modelName, m_modelSourcePath);
        if (dir) {
            (void)syncOperationOpenItem();
        }
    }

    if (!m_onMaterialPartSync || !dir) {
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
            } else if (stockSelection->sheet.area() > 0.0f &&
                       stockSelection->sheet.cost > 0.0f) {
                const f64 blankArea = static_cast<f64>(m_stock.width) *
                                      static_cast<f64>(m_stock.height);
                data.unitRate = static_cast<f64>(stockSelection->sheet.cost) *
                                (blankArea / static_cast<f64>(stockSelection->sheet.area()));
            }
        }
    }

    m_onMaterialPartSync(data);
}

std::optional<i64> DirectCarvePanel::syncOperationOpenItem() {
    if (!m_projectManager || !m_modelLoaded) {
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
        {"material_id", materialId.has_value() ? nlohmann::json(*materialId) : nlohmann::json(nullptr)},
        {"material_name", materialName},
        {"stock", {
            {"width_mm", m_stock.width},
            {"height_mm", m_stock.height},
            {"thickness_mm", m_stock.thickness},
        }},
        {"fit", {
            {"scale", m_fitParams.scale},
            {"offset_x_mm", m_fitParams.offsetX},
            {"offset_y_mm", m_fitParams.offsetY},
            {"depth_mm", m_fitParams.depthMm},
        }},
        {"toolpath", {
            {"scan_axis", scanAxisLabel(m_toolpathConfig.axis)},
            {"mill_direction", millDirectionLabel(m_toolpathConfig.direction)},
            {"stepover_preset", stepoverPresetLabel(m_toolpathConfig.stepoverPreset)},
            {"custom_stepover_pct", m_toolpathConfig.customStepoverPct},
            {"safe_z_mm", m_toolpathConfig.safeZMm},
            {"feed_rate_mm_min", m_toolpathConfig.feedRateMmMin},
            {"plunge_rate_mm_min", m_toolpathConfig.plungeRateMmMin},
            {"lead_in_mm", m_toolpathConfig.leadInMm},
            {"scan_resolution_mm", m_toolpathConfig.scanResolutionMm},
        }},
    };

    nlohmann::json snapshot = {
        {"machine", {
            {"name", profile.name},
            {"max_travel_x_mm", profile.maxTravelX},
            {"max_travel_y_mm", profile.maxTravelY},
            {"max_travel_z_mm", profile.maxTravelZ},
            {"rapid_rate_mm_min", profile.rapidRate},
            {"default_feed_rate_mm_min", profile.defaultFeedRate},
            {"spindle_max_rpm", profile.spindleMaxRPM},
            {"spindle_power_w", profile.spindlePower},
        }},
        {"setup", {
            {"model_loaded", m_modelLoaded},
            {"material_selected", m_materialSelected},
            {"finishing_tool_selected", m_finishingToolSelected},
            {"clearing_tool_selected", m_clearToolSelected},
            {"toolpath_generated", m_toolpathGenerated},
            {"settings_version", m_settingsVersion},
            {"generated_at_version", m_generatedAtVersion},
        }},
    };

    if (m_finishingToolSelected) {
        snapshot["finishing_tool"] = toolSummaryJson(m_finishTool);
    }
    if (m_clearToolSelected) {
        snapshot["clearing_tool"] = toolSummaryJson(m_clearTool);
    }

    ProjectOpenItem item;
    item.itemType = ProjectOpenItemType::Operation;
    item.status = (m_materialSelected && m_finishingToolSelected)
                      ? ProjectOpenItemStatus::Ready
                      : ProjectOpenItemStatus::Planned;
    item.sourceTable = "direct_carve";
    item.sourceKey = "direct_carve:" + ProjectDirectory::sanitizeName(partName);
    item.parentItemId = currentModelOpenItemId();
    item.displayName = "Direct Carve: " + partName;
    item.intentJson = intent.dump();
    item.snapshotJson = snapshot.dump();

    auto operationItemId = m_projectManager->upsertCurrentOpenItem(std::move(item));
    if (operationItemId) {
        const auto operationSourceKey = "direct_carve:" + ProjectDirectory::sanitizeName(partName);
        if (m_finishingToolSelected) {
            (void)syncToolOpenItem(*operationItemId, operationSourceKey, "finish", m_finishTool);
        }
        if (m_clearToolSelected) {
            (void)syncToolOpenItem(*operationItemId, operationSourceKey, "clear", m_clearTool);
        }
    }

    return operationItemId;
}

std::optional<i64> DirectCarvePanel::currentModelOpenItemId() {
    if (!m_projectManager || !m_libraryManager || m_modelSourcePath.empty() ||
        !file::exists(m_modelSourcePath)) {
        return std::nullopt;
    }

    auto model = m_libraryManager->getModelByHash(hash::computeFile(m_modelSourcePath));
    if (!model) {
        return std::nullopt;
    }

    for (const auto& item : m_projectManager->currentOpenItems()) {
        if (item.itemType == ProjectOpenItemType::Model &&
            item.sourceTable == "models" &&
            item.sourceId.has_value() &&
            *item.sourceId == model->id) {
            return item.id;
        }
    }

    return std::nullopt;
}

std::optional<i64> DirectCarvePanel::syncToolOpenItem(i64 operationItemId,
                                                      const std::string& operationSourceKey,
                                                      const std::string& role,
                                                      const VtdbToolGeometry& tool) {
    if (!m_projectManager || operationItemId <= 0) {
        return std::nullopt;
    }

    nlohmann::json intent = {
        {"role", role},
        {"operation_source_key", operationSourceKey},
        {"required_for", role == "clear" ? "clearing_pass" : "finishing_pass"},
    };

    ProjectOpenItem item;
    item.itemType = ProjectOpenItemType::Tool;
    item.sourceTable = "direct_carve";
    item.sourceKey = operationSourceKey + ":tool:" + role;
    item.parentItemId = operationItemId;
    item.status = ProjectOpenItemStatus::Ready;
    item.displayName = (role == "clear" ? "Clearing Tool: " : "Finishing Tool: ") +
                       resolveToolNameFormat(tool);
    item.intentJson = intent.dump();
    item.snapshotJson = toolSummaryJson(tool).dump();

    return m_projectManager->upsertCurrentOpenItem(std::move(item));
}

const char* DirectCarvePanel::stepLabel(Step step) {
    switch (step) {
    case Step::ModelFit:     return "Model";
    case Step::ToolSelect:   return "Tool";
    case Step::MaterialSetup:return "Material";
    case Step::Preview:      return "Preview";
    case Step::MachineCheck: return "Machine";
    case Step::ZeroConfirm:  return "Zero";
    case Step::OutlineTest:  return "Outline";
    case Step::Commit:       return "Confirm";
    case Step::Running:      return "Running";
    }
    return "???";
}

void DirectCarvePanel::render() {
    if (!m_open) return;
    applyMinSize(40.0f, 25.0f);
    if (!ImGui::Begin(m_title.c_str(), &m_open)) { ImGui::End(); return; }

    renderStepIndicator();
    ImGui::Separator();
    ImGui::Spacing();

    switch (m_currentStep) {
    case Step::ModelFit:      renderModelFit();      break;
    case Step::ToolSelect:    renderToolSelect();    break;
    case Step::MaterialSetup: renderMaterialSetup(); break;
    case Step::Preview:       renderPreview();       break;
    case Step::MachineCheck:  renderMachineCheck();  break;
    case Step::ZeroConfirm:   renderZeroConfirm();   break;
    case Step::OutlineTest:   renderOutlineTest();   break;
    case Step::Commit:        renderCommit();        break;
    case Step::Running:       renderRunning();       break;
    }

    ImGui::Spacing();
    ImGui::Separator();
    renderNavButtons();
    ImGui::End();

    // Render floating dialogs owned by this panel
    if (m_profileDialog.isOpen())
        m_profileDialog.render();
}

void DirectCarvePanel::renderStepIndicator() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    float fontSize = ImGui::GetFontSize();
    float circleR = fontSize * 0.35f;
    int curIdx = static_cast<int>(m_currentStep);

    // Evenly space steps across the full available width
    float availW = ImGui::GetContentRegionAvail().x;
    float stepSpacing = (STEP_COUNT > 1) ? availW / static_cast<float>(STEP_COUNT) : availW;
    float totalH = circleR * 2.0f + fontSize + 4.0f; // circle + gap + label

    for (int i = 0; i < STEP_COUNT; ++i) {
        const char* label = stepLabel(static_cast<Step>(i));
        float labelW = ImGui::CalcTextSize(label).x;
        float cx = cursor.x + stepSpacing * (static_cast<float>(i) + 0.5f);
        float cy = cursor.y + circleR;
        ImVec4 color = (i < curIdx) ? kGreen : (i == curIdx) ? kBright : kDimmed;
        ImU32 col = ImGui::ColorConvertFloat4ToU32(color);

        // Clickable invisible button over step region
        ImVec2 hitMin{cx - stepSpacing * 0.5f, cursor.y};
        ImVec2 hitMax{cx + stepSpacing * 0.5f, cursor.y + totalH};
        ImGui::SetCursorScreenPos(hitMin);
        char btnId[32];
        std::snprintf(btnId, sizeof(btnId), "##step%d", i);
        if (ImGui::InvisibleButton(btnId, ImVec2(hitMax.x - hitMin.x, hitMax.y - hitMin.y))) {
            // Allow clicking to any completed step or the current step
            if (i <= curIdx)
                m_currentStep = static_cast<Step>(i);
        }
        bool hovered = ImGui::IsItemHovered();
        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        // Circle
        if (i <= curIdx)
            dl->AddCircleFilled(ImVec2(cx, cy), circleR, col);
        else
            dl->AddCircle(ImVec2(cx, cy), circleR, col, 0, 1.5f);

        // Hover highlight ring
        if (hovered && i <= curIdx)
            dl->AddCircle(ImVec2(cx, cy), circleR + 2.0f, col, 0, 1.5f);

        // Label centered below circle
        dl->AddText(ImVec2(cx - labelW * 0.5f, cy + circleR + 2.0f), col, label);

        // Connecting line to next step
        if (i < STEP_COUNT - 1) {
            float nextCx = cursor.x + stepSpacing * (static_cast<float>(i) + 1.5f);
            float lx0 = cx + circleR + 2.0f;
            float lx1 = nextCx - circleR - 2.0f;
            ImU32 lc = (i < curIdx) ?
                ImGui::ColorConvertFloat4ToU32(kGreen) :
                ImGui::ColorConvertFloat4ToU32(kDimmed);
            dl->AddLine(ImVec2(lx0, cy), ImVec2(lx1, cy), lc, 1.5f);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + totalH + 2.0f));
    ImGui::Dummy(ImVec2(0, 0)); // advance layout cursor
}

void DirectCarvePanel::renderNavButtons() {
    float fontSize = ImGui::GetFontSize();
    float bw = fontSize * 6.0f;
    bool isFirst = (m_currentStep == Step::ModelFit);
    bool isRunning = (m_currentStep == Step::Running);
    bool isCommit = (m_currentStep == Step::Commit);

    if (isFirst || isRunning) ImGui::BeginDisabled();
    if (ImGui::Button("Back", ImVec2(bw, 0))) retreatStep();
    if (isFirst || isRunning) ImGui::EndDisabled();

    ImGui::SameLine();
    bool canGo = canAdvance();
    if (!canGo) ImGui::BeginDisabled();
    if (ImGui::Button(isCommit ? "Start Carving" : "Next", ImVec2(bw, 0))) advanceStep();
    if (!canGo) ImGui::EndDisabled();

    ImGui::SameLine();
    if (isRunning) ImGui::BeginDisabled();
    if (ImGui::Button("Cancel", ImVec2(bw, 0))) {
        m_currentStep = Step::ModelFit;
        m_safeZConfirmed = false;
        m_finishingToolSelected = false;
        m_materialSelected = false;
        m_toolpathGenerated = false;
        m_settingsVersion = 0;
        m_generatedAtVersion = -1;
        m_outlineCompleted = false;
        m_outlineSkipped = false;
        m_outlineRunning = false;
        m_zeroConfirmed = false;
        m_commitConfirmed = false;
    }
    if (isRunning) ImGui::EndDisabled();
}

bool DirectCarvePanel::canAdvance() const {
    switch (m_currentStep) {
    // Planning steps — no machine required
    case Step::ModelFit:      return m_modelLoaded;
    case Step::ToolSelect:    return m_finishingToolSelected;
    case Step::MaterialSetup: return m_materialSelected;
    case Step::Preview:       return m_toolpathGenerated
                                     && (m_generatedAtVersion == m_settingsVersion);
    // Machine steps — CNC required
    case Step::MachineCheck:  return validateMachineReady();
    case Step::ZeroConfirm:   return m_zeroConfirmed;
    case Step::OutlineTest:   return m_outlineCompleted || m_outlineSkipped;
    case Step::Commit:        return m_commitConfirmed;
    case Step::Running:       return false;
    }
    return false;
}

void DirectCarvePanel::advanceStep() {
    if (m_currentStep == Step::ModelFit || m_currentStep == Step::MaterialSetup) {
        syncSetupToOptimizerAndProject();
    }
    int idx = static_cast<int>(m_currentStep);
    if (idx < STEP_COUNT - 1) m_currentStep = static_cast<Step>(idx + 1);
}

void DirectCarvePanel::retreatStep() {
    int idx = static_cast<int>(m_currentStep);
    if (idx > 0) m_currentStep = static_cast<Step>(idx - 1);
}

bool DirectCarvePanel::validateMachineReady() const {
    if (!m_cncConnected) return false;
    if (m_machineStatus.state == MachineState::Alarm) return false;
    if (m_machineStatus.state == MachineState::Unknown) return false;
    if (!m_safeZConfirmed) return false;
    return true;
}

void DirectCarvePanel::renderMachineCheck() {
    ImGui::TextUnformatted("Machine Checklist");
    ImGui::Spacing();

    bool connected = m_cncConnected;
    bool idle = (m_machineStatus.state == MachineState::Idle);
    bool notAlarm = (m_machineStatus.state != MachineState::Alarm &&
                     m_machineStatus.state != MachineState::Unknown);
    auto& cfg = Config::instance();
    const auto& profile = cfg.getActiveMachineProfile();
    bool profileOk = (profile.maxTravelX > 0.0f && profile.maxTravelY > 0.0f &&
                      profile.maxTravelZ > 0.0f);

    // Show detected machine
    if (connected && profileOk) {
        ImGui::Text("Machine: %s", profile.name.c_str());
    } else if (connected) {
        ImGui::TextColored(kYellow, "Machine: Connected (no profile configured)");
    } else {
        ImGui::TextColored(kRed, "Machine: Not connected");
    }
    ImGui::Spacing();

    // Checklist
    statusBullet(connected, "CNC connected");
    statusBullet(notAlarm, "No alarm");
    statusBullet(idle, "Machine idle");
    statusBullet(profileOk, "Machine profile configured");
    statusBullet(m_safeZConfirmed, "Safe Z verified");
    ImGui::Spacing();

    bool canSend = (m_cnc != nullptr && connected);
    float fs = ImGui::GetFontSize();
    float bw = fs * 10.0f;

    // Homing
    if (!canSend) ImGui::BeginDisabled();
    if (ImGui::Button("Home Machine", ImVec2(bw, 0))) {
        m_cnc->sendCommand("$H");
    }
    if (!canSend) ImGui::EndDisabled();

    // Safe Z test
    ImGui::Spacing();
    if (connected && idle) {
        if (ImGui::Button("Test Safe Z", ImVec2(bw, 0))) {
            if (m_cnc) {
                char cmd[64];
                std::snprintf(cmd, sizeof(cmd), "G0 Z%.3f",
                              static_cast<double>(m_toolpathConfig.safeZMm));
                m_cnc->sendCommand(cmd);
                m_safeZConfirmed = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Skip (confirm safe Z)", ImVec2(bw * 1.2f, 0)))
            m_safeZConfirmed = true;
    }

    if (m_machineStatus.state == MachineState::Alarm) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, kRed);
        ImGui::TextWrapped("Machine is in ALARM state. Unlock or reset before continuing.");
        ImGui::PopStyleColor();
        if (canSend) {
            if (ImGui::Button("Unlock ($X)", ImVec2(bw, 0)))
                m_cnc->sendCommand("$X");
        }
    }
}

// --- renderModelFit (18-02): Stock dims, scale/depth/position, live fit, heightmap gen ---

void DirectCarvePanel::renderModelFit() {
    if (!m_modelLoaded) {
        ImGui::TextColored(kYellow, "No model loaded. Load an STL model first.");
        return;
    }

    float totalW = ImGui::GetContentRegionAvail().x;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float thumbW = (totalW - spacing) / 3.0f;
    float controlsW = totalW - thumbW - spacing;

    // Left column: thumbnail filling 1/3 width
    ImGui::BeginChild("##thumb_col", ImVec2(thumbW, 0), false);
    if (m_modelThumbnail != 0) {
        // Square thumbnail filling the column width
        ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(m_modelThumbnail)),
                     ImVec2(thumbW, thumbW));
    }
    // Model name + natural dimensions below thumbnail
    ImGui::TextWrapped("%s", m_modelName.c_str());
    Vec3 natSize = m_modelBoundsMax - m_modelBoundsMin;
    ImGui::TextDisabled("%.1f x %.1f x %.1f mm",
                        static_cast<double>(natSize.x),
                        static_cast<double>(natSize.y),
                        static_cast<double>(natSize.z));
    if (!m_materialName.empty())
        ImGui::TextDisabled("%s", m_materialName.c_str());
    ImGui::EndChild();

    ImGui::SameLine();

    // Right column: all controls in 2/3 width
    ImGui::BeginChild("##controls_col", ImVec2(controlsW, 0), false);

    // Stock dimensions — auto-fit recalculates whenever stock changes
    float iw = ImGui::GetFontSize() * 8.0f;
    ImGui::Text("Stock Dimensions:");
    auto prevStock = m_stock;
    ImGui::SetNextItemWidth(iw);
    ImGui::InputFloat("Width (X) mm", &m_stock.width, 1.0f, 10.0f, "%.1f");
    m_stock.width = std::clamp(m_stock.width, 1.0f, 2000.0f);
    ImGui::SetNextItemWidth(iw);
    ImGui::InputFloat("Height (Y) mm", &m_stock.height, 1.0f, 10.0f, "%.1f");
    m_stock.height = std::clamp(m_stock.height, 1.0f, 2000.0f);
    ImGui::SetNextItemWidth(iw);
    ImGui::InputFloat("Thickness (Z) mm", &m_stock.thickness, 0.5f, 5.0f, "%.1f");
    m_stock.thickness = std::clamp(m_stock.thickness, 0.5f, 200.0f);
    bool stockChanged = (m_stock.width != prevStock.width ||
                         m_stock.height != prevStock.height ||
                         m_stock.thickness != prevStock.thickness);

    float fs = ImGui::GetFontSize();
    float bw = fs * 12.0f;
    if (ImGui::Button("From Machine Profile", ImVec2(bw, 0))) {
        const auto& prof = Config::instance().getActiveMachineProfile();
        m_stock.width = prof.maxTravelX;
        m_stock.height = prof.maxTravelY;
        m_stock.thickness = prof.maxTravelZ;
        stockChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Edit Profile")) {
        m_profileDialog.setOnProfileChanged([this]() {
            const auto& prof = Config::instance().getActiveMachineProfile();
            m_stock.width = prof.maxTravelX;
            m_stock.height = prof.maxTravelY;
            m_stock.thickness = prof.maxTravelZ;
        });
        m_profileDialog.open();
    }

    // Cut list integration
    if (m_cutOptimizer) {
        ImGui::SameLine();
        if (ImGui::Button("From Cut List"))
            ImGui::OpenPopup("PickCutListPart");

        if (ImGui::BeginPopup("PickCutListPart")) {
            const auto& parts = m_cutOptimizer->parts();
            if (parts.empty()) {
                ImGui::TextDisabled("No parts in cut list.");
            } else {
                ImGui::TextUnformatted("Select a cut piece:");
                ImGui::Separator();
                for (size_t i = 0; i < parts.size(); ++i) {
                    const auto& p = parts[i];
                    char label[128];
                    std::snprintf(label, sizeof(label), "%s  %.1f x %.1f mm",
                                  p.name.empty() ? "Part" : p.name.c_str(),
                                  static_cast<double>(p.width),
                                  static_cast<double>(p.height));
                    if (ImGui::Selectable(label)) {
                        m_stock.width = p.width;
                        m_stock.height = p.height;
                        if (p.materialId > 0) {
                            if (!m_materialListLoaded && m_materialMgr) {
                                m_materialListLoaded = true;
                                m_materialList = m_materialMgr->getAllMaterials();
                            }
                            for (int mi = 0; mi < static_cast<int>(m_materialList.size()); ++mi) {
                                const auto& mat = m_materialList[static_cast<size_t>(mi)];
                                if (mat.id == p.materialId) {
                                    m_selectedMaterialIdx = mi;
                                    m_materialName = mat.name;
                                    m_materialSelected = true;
                                    break;
                                }
                            }
                        }
                        if (auto stock = m_cutOptimizer->currentStockSelection()) {
                            if (stock->stockSize && stock->stockSize->thicknessMm > 0.0) {
                                m_stock.thickness = static_cast<f32>(stock->stockSize->thicknessMm);
                            }
                        }
                        stockChanged = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndPopup();
        }
    }

    // Auto-fit whenever stock dimensions change
    if (stockChanged) {
        m_fitter.setStock(m_stock);
        m_fitParams.scale = m_fitter.autoScale();
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // Scale slider: 10% .. 100% (never upscale beyond original model size)
    // Internal value is 0..1 factor; display as percentage
    f32 scalePct = m_fitParams.scale * 100.0f;
    ImGui::SetNextItemWidth(iw);
    if (ImGui::SliderFloat("Scale", &scalePct, 10.0f, 100.0f, "%.1f %%"))
        m_fitParams.scale = scalePct / 100.0f;
    ImGui::SameLine();
    if (ImGui::Button("Auto Fit")) {
        m_fitter.setStock(m_stock);
        m_fitParams.scale = m_fitter.autoScale();
    }

    f32 depthMax = std::max(m_stock.thickness, 1.0f);
    ImGui::SetNextItemWidth(iw);
    ImGui::DragFloat("Depth (Z) mm", &m_fitParams.depthMm, 0.1f, 0.0f, depthMax, "%.1f");
    ImGui::SameLine();
    if (ImGui::Button("Full Depth")) {
        m_fitter.setStock(m_stock);
        m_fitParams.depthMm = m_fitter.autoDepth() * m_fitParams.scale;
    }

    ImGui::SetNextItemWidth(iw);
    ImGui::DragFloat2("Position (XY)", &m_fitParams.offsetX, 0.5f);
    ImGui::SameLine();
    if (ImGui::Button("Corner")) {
        m_fitParams.offsetX = 0.0f;
        m_fitParams.offsetY = 0.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Center")) {
        f32 modelW = (m_modelBoundsMax.x - m_modelBoundsMin.x) * m_fitParams.scale;
        f32 modelH = (m_modelBoundsMax.y - m_modelBoundsMin.y) * m_fitParams.scale;
        m_fitParams.offsetX = (m_stock.width - modelW) * 0.5f;
        m_fitParams.offsetY = (m_stock.height - modelH) * 0.5f;
    }

    // Live fit result
    m_fitter.setStock(m_stock);
    const auto& mp = Config::instance().getActiveMachineProfile();
    m_fitter.setMachineTravel(mp.maxTravelX, mp.maxTravelY, mp.maxTravelZ);
    carve::FitResult result = m_fitter.fit(m_fitParams);

    // Notify viewport of FitParams changes for alignment overlay
    if (m_onFitParamsChanged) {
        m_onFitParamsChanged(m_fitParams, m_modelBoundsMin, m_modelBoundsMax, m_stock);
    }

    ImGui::Spacing();
    Vec3 dim = result.modelMax - result.modelMin;
    ImGui::Text("After transform: %.1f x %.1f x %.1f mm",
                static_cast<double>(dim.x), static_cast<double>(dim.y),
                static_cast<double>(dim.z));
    ImGui::TextColored(result.fitsStock ? kGreen : kRed,
                       result.fitsStock ? "Fits stock" : "Exceeds stock");
    ImGui::SameLine();
    ImGui::TextColored(result.fitsMachine ? kGreen : kRed,
                       result.fitsMachine ? "Fits machine" : "Exceeds machine");
    if (!result.warning.empty())
        ImGui::TextColored(kYellow, "%s", result.warning.c_str());

    // Cut list integration: push carve blank as a cut piece
    if (m_cutOptimizer && m_modelLoaded) {
        ImGui::Spacing();
        if (ImGui::Button("Sync Setup", ImVec2(bw, 0))) {
            syncSetupToOptimizerAndProject();
            char msg[64];
            std::snprintf(msg, sizeof(msg), "Synced %.0fx%.0f mm setup",
                          static_cast<double>(m_stock.width),
                          static_cast<double>(m_stock.height));
            ToastManager::instance().show(ToastType::Success, msg);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Updates the cut optimizer part and project material line.");

        // Show scrap: stock area vs carve footprint
        f32 stockArea = m_stock.width * m_stock.height;
        f32 carveArea = dim.x * dim.y;
        if (stockArea > 0.0f && carveArea > 0.0f && carveArea < stockArea) {
            f32 usedPct = (carveArea / stockArea) * 100.0f;
            f32 scrapArea = stockArea - carveArea;
            ImGui::TextDisabled("Stock usage: %.0f%%  (%.0f mm%c scrap)",
                                static_cast<double>(usedPct),
                                static_cast<double>(scrapArea),
                                '\xB2');
        }
    }

    ImGui::EndChild(); // ##controls_col
}

void DirectCarvePanel::renderToolSelect() {
    ImGui::TextUnformatted("Tool Selection");
    ImGui::Spacing();
    ImGui::TextWrapped("Select a finishing tool for the carve operation. "
                       "A ball-nose end mill is recommended for smooth 3D relief surfaces. "
                       "Smaller diameters capture finer detail but take longer.");
    ImGui::Spacing();

    // Load tools from database on first visit
    if (!m_toolLibraryLoaded && m_toolDb) {
        m_toolLibraryLoaded = true;
        m_toolboxTools.clear();
        m_allTools.clear();

        auto isCarveType = [](VtdbToolType t) {
            return t == VtdbToolType::BallNose || t == VtdbToolType::TaperedBallNose ||
                   t == VtdbToolType::VBit || t == VtdbToolType::EndMill;
        };

        // Load toolbox subset
        if (m_toolboxRepo) {
            auto ids = m_toolboxRepo->getAllGeometryIds();
            for (const auto& id : ids) {
                auto geom = m_toolDb->findGeometryById(id);
                if (geom && isCarveType(geom->tool_type))
                    m_toolboxTools.push_back(std::move(*geom));
            }
        }

        // Load full library
        auto allGeoms = m_toolDb->findAllGeometries();
        for (auto& g : allGeoms) {
            if (isCarveType(g.tool_type))
                m_allTools.push_back(std::move(g));
        }

        // Default to toolbox if it has tools, otherwise show all
        m_showAllTools = m_toolboxTools.empty();
        m_libraryTools = m_showAllTools ? m_allTools : m_toolboxTools;
    }

    // Source tabs: Library vs Manual
    bool hasLibrary = !m_libraryTools.empty();

    if (hasLibrary || !m_allTools.empty()) {
        if (ImGui::BeginTabBar("##toolSource")) {
            if (ImGui::BeginTabItem("Tool Library")) {
                m_useManualTool = false;
                ImGui::Spacing();

                // Filter toggle + Edit Toolbox button
                bool hasToolboxTools = !m_toolboxTools.empty();
                if (hasToolboxTools) {
                    bool showAll = m_showAllTools;
                    if (ImGui::RadioButton("My Toolbox", !showAll)) {
                        m_showAllTools = false;
                        m_libraryTools = m_toolboxTools;
                        m_selectedLibToolIdx = -1;
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("All Tools", showAll)) {
                        m_showAllTools = true;
                        m_libraryTools = m_allTools;
                        m_selectedLibToolIdx = -1;
                    }
                } else {
                    ImGui::TextDisabled("Showing all tools (My Toolbox is empty)");
                }

                ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Edit Toolbox").x
                                - ImGui::GetStyle().FramePadding.x * 2);
                if (ImGui::SmallButton("Edit Toolbox")) {
                    if (m_openToolBrowser) m_openToolBrowser();
                    ImGui::SetWindowFocus("Tool Browser");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Open Tool Browser to manage My Toolbox");

                ImGui::Spacing();
                renderToolLibraryPicker();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Manual Entry")) {
                m_useManualTool = true;
                ImGui::Spacing();
                renderManualToolEntry();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    } else {
        if (!m_toolDb) {
            ImGui::TextColored(kYellow, "No tool database connected.");
        } else {
            ImGui::TextDisabled("Tool library is empty. Import tools via the Tool Browser panel.");
        }
        ImGui::Spacing();
        m_useManualTool = true;
        renderManualToolEntry();
    }

    // Show summary of selected tool
    if (m_finishingToolSelected) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const char* typeStr = "Tool";
        switch (m_finishTool.tool_type) {
        case VtdbToolType::BallNose:        typeStr = "Ball Nose"; break;
        case VtdbToolType::TaperedBallNose: typeStr = "Tapered Ball Nose"; break;
        case VtdbToolType::VBit:            typeStr = "V-Bit"; break;
        case VtdbToolType::EndMill:         typeStr = "End Mill"; break;
        default: break;
        }
        ImGui::TextColored(kGreen, "%s Finishing: %s  %.3gmm  %d flute%s",
                           Icons::Check, typeStr, diameterMm(m_finishTool),
                           m_finishTool.num_flutes,
                           m_finishTool.num_flutes != 1 ? "s" : "");

        // Run recommender when heightmap analysis is available
        if (m_carveJob && m_carveJob->state() == carve::CarveJobState::Ready
            && m_toolDb && !m_recommendationRun) {
            // Analyze heightmap for islands using selected finishing tool angle
            f32 toolAngle = static_cast<f32>(m_finishTool.included_angle);
            if (toolAngle <= 0.0f) toolAngle = 90.0f;
            m_carveJob->analyzeHeightmap(toolAngle);

            // Populate recommender with toolbox tools
            carve::ToolRecommender recommender;
            VtdbCuttingData emptyData{};
            for (const auto& g : m_libraryTools) {
                auto entities = m_toolDb->findEntitiesForGeometry(g.id);
                if (!entities.empty()) {
                    auto cd = m_toolDb->findCuttingDataById(entities[0].tool_cutting_data_id);
                    recommender.addCandidate(g, cd.value_or(emptyData));
                } else {
                    recommender.addCandidate(g, emptyData);
                }
            }

            const auto& hm = m_carveJob->heightmap();
            carve::RecommendationInput input;
            input.curvature = m_carveJob->curvatureResult();
            input.islands = m_carveJob->islandResult();
            input.modelDepthMm = hm.maxZ() - hm.minZ();
            input.stockThicknessMm = m_stock.thickness;
            m_recommendation = recommender.recommend(input);
            m_recommendationRun = true;

            // Auto-select top clearing tool
            if (m_recommendation.needsClearing && !m_recommendation.clearing.empty()) {
                m_clearTool = m_recommendation.clearing[0].geometry;
                m_clearToolSelected = true;
                m_selectedClearIdx = 0;
            }
        }

        // Show clearing tool section when recommendation has been run
        if (m_recommendationRun) {
            ImGui::Spacing();

            if (m_recommendation.needsClearing) {
                const auto& islands = m_carveJob->islandResult().islands;
                f32 maxDepth = 0.0f;
                for (const auto& island : islands)
                    maxDepth = std::max(maxDepth, island.depth);

                ImGui::TextColored(kYellow, "%s %d island%s detected (max depth: %.1fmm)",
                                   Icons::Warning,
                                   static_cast<int>(islands.size()),
                                   islands.size() != 1 ? "s" : "",
                                   static_cast<double>(maxDepth));
                ImGui::TextWrapped("A roughing pass will clear material around islands before "
                                   "the finishing pass, preventing deep plunges with the finishing tool.");

                ImGui::Spacing();

                if (!m_recommendation.clearing.empty()) {
                    ImGui::Text("Clearing Tool:");
                    ImGui::Indent();
                    for (int i = 0; i < static_cast<int>(m_recommendation.clearing.size()); ++i) {
                        const auto& tc = m_recommendation.clearing[static_cast<size_t>(i)];
                        bool selected = (i == m_selectedClearIdx);

                        char label[192];
                        const char* clearTypeStr = (tc.geometry.tool_type == VtdbToolType::EndMill)
                            ? "End Mill" : "Ball Nose";
                        std::snprintf(label, sizeof(label), "%s %.3gmm  (score: %.0f%%)",
                                      clearTypeStr,
                                      tc.geometry.diameter,
                                      static_cast<double>(tc.score * 100.0f));

                        if (ImGui::Selectable(label, selected)) {
                            bool changed = false;
                            if (selected) {
                                changed = m_clearToolSelected;
                                m_selectedClearIdx = -1;
                                m_clearToolSelected = false;
                            } else {
                                changed = (m_selectedClearIdx != i);
                                m_selectedClearIdx = i;
                                m_clearTool = tc.geometry;
                                m_clearToolSelected = true;
                            }
                            if (changed) ++m_settingsVersion;
                        }
                        // Show reasoning as tooltip
                        if (ImGui::IsItemHovered() && !tc.reasoning.empty())
                            ImGui::SetTooltip("%s", tc.reasoning.c_str());
                    }
                    ImGui::Unindent();
                } else {
                    ImGui::TextColored(kYellow,
                        "No suitable clearing tools in your toolbox. "
                        "Add an end mill or ball nose to My Toolbox.");
                }

                if (m_clearToolSelected) {
                    const char* clrType = (m_clearTool.tool_type == VtdbToolType::EndMill)
                        ? "End Mill" : "Ball Nose";
                    ImGui::TextColored(kGreen, "%s Clearing: %s  %.3gmm",
                                       Icons::Check, clrType, diameterMm(m_clearTool));
                }
            } else {
                ImGui::TextColored(kGreen, "%s No islands detected - no roughing pass needed.",
                                   Icons::Check);
            }
        }
    }
}

void DirectCarvePanel::renderToolLibraryPicker() {
    float availH = ImGui::GetContentRegionAvail().y
                   - ImGui::GetFrameHeightWithSpacing() * 3.0f; // room for nav buttons
    if (availH < ImGui::GetFrameHeightWithSpacing() * 3.0f)
        availH = ImGui::GetFrameHeightWithSpacing() * 3.0f;

    ImGui::BeginChild("##toolList", ImVec2(0, availH), ImGuiChildFlags_Borders);
    for (int i = 0; i < static_cast<int>(m_libraryTools.size()); ++i) {
        const auto& g = m_libraryTools[static_cast<size_t>(i)];

        // Type badge color
        ImVec4 badgeColor = ImGui::ColorConvertU32ToFloat4(Theme::Colors::Secondary);
        const char* typeLabel = "Tool";
        switch (g.tool_type) {
        case VtdbToolType::BallNose:
            badgeColor = ImGui::ColorConvertU32ToFloat4(Theme::Colors::Primary);
            typeLabel = "Ball Nose";
            break;
        case VtdbToolType::TaperedBallNose:
            badgeColor = ImGui::ColorConvertU32ToFloat4(Theme::Colors::Warning);
            typeLabel = "TBN";
            break;
        case VtdbToolType::VBit:
            badgeColor = ImGui::ColorConvertU32ToFloat4(Theme::Colors::Success);
            typeLabel = "V-Bit";
            break;
        case VtdbToolType::EndMill:
            badgeColor = ImGui::ColorConvertU32ToFloat4(Theme::Colors::Error);
            typeLabel = "End Mill";
            break;
        default: break;
        }

        ImGui::PushID(i);
        bool selected = (i == m_selectedLibToolIdx);

        // Selectable row
        std::string resolved = resolveToolNameFormat(g);
        char label[128];
        std::snprintf(label, sizeof(label), "%s", resolved.c_str());

        if (ImGui::Selectable(label, selected, 0, ImVec2(0, 0))) {
            if (m_selectedLibToolIdx != i) {
                m_selectedLibToolIdx = i;
                m_finishTool = g;
                m_finishingToolSelected = true;
                m_recommendationRun = false;
                ++m_settingsVersion;
            }
        }

        // Inline specs on same line
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
        ImGui::PushStyleColor(ImGuiCol_Button, badgeColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, badgeColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, badgeColor);
        ImGui::SmallButton(typeLabel);
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::TextDisabled("%.3gmm", diameterMm(g));

        if (g.tool_type == VtdbToolType::VBit && g.included_angle > 0.0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%.0f deg", g.included_angle);
        }

        ImGui::PopID();
    }
    ImGui::EndChild();
}

void DirectCarvePanel::renderManualToolEntry() {
    float iw = ImGui::GetFontSize() * 8.0f;

    const char* typeNames[] = {"Ball Nose", "V-Bit", "End Mill", "Tapered Ball Nose"};
    ImGui::SetNextItemWidth(iw);
    ImGui::Combo("Tool Type", &m_manualToolType, typeNames, 4);

    ImGui::SetNextItemWidth(iw);
    ImGui::InputFloat("Diameter (mm)", &m_manualDiameter, 0.1f, 1.0f, "%.3f");
    m_manualDiameter = std::clamp(m_manualDiameter, 0.1f, 50.0f);

    ImGui::SetNextItemWidth(iw);
    ImGui::InputInt("Flutes", &m_manualFlutes);
    m_manualFlutes = std::clamp(m_manualFlutes, 1, 8);

    // Type-specific fields
    if (m_manualToolType == 1) { // V-Bit
        ImGui::SetNextItemWidth(iw);
        ImGui::InputFloat("Included Angle (deg)", &m_manualAngle, 1.0f, 10.0f, "%.1f");
        m_manualAngle = std::clamp(m_manualAngle, 10.0f, 180.0f);
    }
    if (m_manualToolType == 0 || m_manualToolType == 3) { // Ball Nose / TBN
        ImGui::SetNextItemWidth(iw);
        float halfDia = m_manualDiameter * 0.5f;
        ImGui::InputFloat("Tip Radius (mm)", &m_manualTipRadius, 0.1f, 0.5f, "%.3f");
        m_manualTipRadius = std::clamp(m_manualTipRadius, 0.05f, halfDia);
    }

    ImGui::Spacing();
    bool canAccept = m_manualDiameter > 0.0f;
    if (!canAccept) ImGui::BeginDisabled();
    if (ImGui::Button("Use This Tool")) {
        VtdbToolType types[] = {
            VtdbToolType::BallNose, VtdbToolType::VBit,
            VtdbToolType::EndMill, VtdbToolType::TaperedBallNose
        };
        m_finishTool = VtdbToolGeometry{};
        m_finishTool.tool_type = types[m_manualToolType];
        m_finishTool.diameter = static_cast<f64>(m_manualDiameter);
        m_finishTool.num_flutes = m_manualFlutes;
        m_finishTool.included_angle = static_cast<f64>(m_manualAngle);
        m_finishTool.tip_radius = static_cast<f64>(m_manualTipRadius);
        m_finishTool.units = VtdbUnits::Metric;
        m_finishingToolSelected = true;
        m_recommendationRun = false;
        ++m_settingsVersion;
    }
    if (!canAccept) ImGui::EndDisabled();
}

void DirectCarvePanel::renderMaterialSetup() {
    ImGui::TextUnformatted("Material & Feeds");
    ImGui::Spacing();
    ImGui::TextWrapped("Select the workpiece material and confirm feed rates.");
    ImGui::Spacing();

    // Load materials from database on first visit
    if (!m_materialListLoaded && m_materialMgr) {
        m_materialListLoaded = true;
        m_materialList = m_materialMgr->getAllMaterials();
    }

    float iw = ImGui::GetContentRegionAvail().x * 0.45f;

    // --- Material selector ---
    if (!m_materialList.empty()) {
        ImGui::SetNextItemWidth(iw);
        const char* preview = (m_selectedMaterialIdx >= 0)
            ? m_materialList[static_cast<size_t>(m_selectedMaterialIdx)].name.c_str()
            : "Select material...";
        if (ImGui::BeginCombo("Material", preview)) {
            // Group by category
            const char* catLabels[] = {"Hardwood", "Softwood", "Domestic", "Composite"};
            MaterialCategory cats[] = {
                MaterialCategory::Hardwood, MaterialCategory::Softwood,
                MaterialCategory::Domestic, MaterialCategory::Composite
            };
            for (int c = 0; c < 4; ++c) {
                bool hasItems = false;
                for (int i = 0; i < static_cast<int>(m_materialList.size()); ++i) {
                    if (m_materialList[static_cast<size_t>(i)].category != cats[c])
                        continue;
                    if (!hasItems) {
                        ImGui::SeparatorText(catLabels[c]);
                        hasItems = true;
                    }
                    const auto& mat = m_materialList[static_cast<size_t>(i)];
                    bool selected = (i == m_selectedMaterialIdx);
                    if (ImGui::Selectable(mat.name.c_str(), selected)) {
                        m_selectedMaterialIdx = i;
                        m_materialName = mat.name;
                        m_materialSelected = true;

                        // Auto-calculate feed rates using ToolCalculator
                        {
                            CalcInput ci;
                            ci.diameter = m_finishTool.diameter;
                            if (ci.diameter <= 0.0)
                                ci.diameter = 3.175; // 1/8" fallback
                            ci.num_flutes = m_finishTool.num_flutes;
                            ci.tool_type = m_finishTool.tool_type;
                            ci.units = m_finishTool.units;
                            ci.janka_hardness = static_cast<f64>(mat.jankaHardness);
                            ci.material_name = mat.name;

                            // Pull machine params from active Config profile
                            const auto& mp = Config::instance().getActiveMachineProfile();
                            ci.spindle_power_watts = static_cast<f64>(mp.spindlePower);
                            ci.max_rpm = static_cast<int>(mp.spindleMaxRPM);
                            switch (mp.driveSystem) {
                            case gcode::DriveSystem::Belt:      ci.drive_type = DriveType::Belt; break;
                            case gcode::DriveSystem::BallScrew: ci.drive_type = DriveType::BallScrew; break;
                            default:                            ci.drive_type = DriveType::LeadScrew; break;
                            }

                            auto result = ToolCalculator::calculate(ci);

                            // Result is in native units; convert to mm/min
                            f64 feedMm = result.feed_rate;
                            f64 plungeMm = result.plunge_rate;
                            if (ci.units == VtdbUnits::Imperial) {
                                feedMm *= 25.4;
                                plungeMm *= 25.4;
                            }

                            // Round to nearest 50
                            m_toolpathConfig.feedRateMmMin =
                                std::round(static_cast<f32>(feedMm) / 50.0f) * 50.0f;
                            m_toolpathConfig.plungeRateMmMin =
                                std::round(static_cast<f32>(plungeMm) / 50.0f) * 50.0f;

                            m_toolpathConfig.feedRateMmMin =
                                std::clamp(m_toolpathConfig.feedRateMmMin, 100.0f, 10000.0f);
                            m_toolpathConfig.plungeRateMmMin =
                                std::clamp(m_toolpathConfig.plungeRateMmMin, 50.0f, 5000.0f);
                        }
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    } else {
        if (!m_materialMgr) {
            ImGui::TextColored(kYellow, "No material database available.");
        } else {
            ImGui::TextDisabled("No materials in library.");
        }
    }

    // Show Janka hardness + machine profile if selected
    if (m_selectedMaterialIdx >= 0) {
        const auto& mat = m_materialList[static_cast<size_t>(m_selectedMaterialIdx)];
        if (mat.jankaHardness > 0.0f) {
            ImGui::SameLine();
            ImGui::TextDisabled("Janka: %.0f lbf", static_cast<double>(mat.jankaHardness));
        }
    }

    // Show which machine profile is driving the calculation
    {
        const auto& mp = Config::instance().getActiveMachineProfile();
        ImGui::TextDisabled("Machine: %s (%.0f RPM, %.0fW, %s)",
                            mp.name.c_str(),
                            static_cast<double>(mp.spindleMaxRPM),
                            static_cast<double>(mp.spindlePower),
                            mp.driveSystem == gcode::DriveSystem::Belt ? "Belt" :
                            mp.driveSystem == gcode::DriveSystem::BallScrew ? "Ball Screw" :
                            mp.driveSystem == gcode::DriveSystem::Acme ? "Acme" : "Lead Screw");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Feed Rates");

    // --- Feed rate inputs (wider, typeable) ---
    ImGui::SetNextItemWidth(iw);
    f32 prevFeed = m_toolpathConfig.feedRateMmMin;
    ImGui::InputFloat("Feed Rate (mm/min)", &m_toolpathConfig.feedRateMmMin, 50.0f, 200.0f, "%.0f");
    m_toolpathConfig.feedRateMmMin = std::clamp(m_toolpathConfig.feedRateMmMin, 10.0f, 20000.0f);
    if (m_toolpathConfig.feedRateMmMin != prevFeed) ++m_settingsVersion;

    ImGui::SetNextItemWidth(iw);
    f32 prevPlunge = m_toolpathConfig.plungeRateMmMin;
    ImGui::InputFloat("Plunge Rate (mm/min)", &m_toolpathConfig.plungeRateMmMin, 10.0f, 50.0f, "%.0f");
    m_toolpathConfig.plungeRateMmMin = std::clamp(m_toolpathConfig.plungeRateMmMin, 5.0f, 5000.0f);
    if (m_toolpathConfig.plungeRateMmMin != prevPlunge) ++m_settingsVersion;

    ImGui::SetNextItemWidth(iw);
    f32 prevSafeZ = m_toolpathConfig.safeZMm;
    ImGui::InputFloat("Safe Z (mm)", &m_toolpathConfig.safeZMm, 0.5f, 2.0f, "%.1f");
    m_toolpathConfig.safeZMm = std::clamp(m_toolpathConfig.safeZMm, 1.0f, 50.0f);
    if (m_toolpathConfig.safeZMm != prevSafeZ) ++m_settingsVersion;

    ImGui::SetNextItemWidth(iw);
    const char* stepoverLabels[] = {"Ultra Fine (1%)", "Fine (8%)", "Basic (12%)", "Rough (25%)", "Roughing (40%)"};
    int stepIdx = static_cast<int>(m_toolpathConfig.stepoverPreset);
    if (ImGui::Combo("Stepover", &stepIdx, stepoverLabels, 5)) {
        m_toolpathConfig.stepoverPreset = static_cast<carve::StepoverPreset>(stepIdx);
        ++m_settingsVersion;
    }

    // Toolpath point resolution along scan lines
    ImGui::SetNextItemWidth(iw);
    f32 hmRes = 0.1f;
    if (m_carveJob && m_carveJob->state() == carve::CarveJobState::Ready)
        hmRes = m_carveJob->heightmap().resolution();
    if (m_toolpathConfig.scanResolutionMm <= 0.0f)
        m_toolpathConfig.scanResolutionMm = std::max(hmRes, 0.2f);
    if (ImGui::SliderFloat("Path Detail (mm)", &m_toolpathConfig.scanResolutionMm,
                            hmRes, 2.0f, "%.2f"))
        ++m_settingsVersion;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Point spacing along each scan line.\n"
                          "Lower = more detail, more G-code lines.\n"
                          "Heightmap resolution: %.2f mm",
                          static_cast<double>(hmRes));

    ImGui::Spacing();
    ImGui::SeparatorText("Scan Pattern");

    ImGui::SetNextItemWidth(iw);
    const char* axisLabels[] = {"X Only", "Y Only", "X then Y", "Y then X"};
    int axisIdx = static_cast<int>(m_toolpathConfig.axis);
    if (ImGui::Combo("Scan Axis", &axisIdx, axisLabels, 4)) {
        m_toolpathConfig.axis = static_cast<carve::ScanAxis>(axisIdx);
        ++m_settingsVersion;
    }

    ImGui::SetNextItemWidth(iw);
    const char* dirLabels[] = {"Climb", "Conventional", "Alternating (Zigzag)"};
    int dirIdx = static_cast<int>(m_toolpathConfig.direction);
    if (ImGui::Combo("Mill Direction", &dirIdx, dirLabels, 3)) {
        m_toolpathConfig.direction = static_cast<carve::MillDirection>(dirIdx);
        ++m_settingsVersion;
    }

    // Auto-confirm when material is selected
    if (!m_materialSelected && m_materialList.empty()) {
        ImGui::Spacing();
        ImGui::Checkbox("Confirm settings", &m_materialSelected);
    }
}

// --- renderPreview (18-02): Heightmap overlay, toolpath lines, stats, controls ---

void DirectCarvePanel::renderPreview() {
    ImGui::TextUnformatted("Toolpath Preview");
    ImGui::Spacing();

    if (!m_carveJob) {
        ImGui::TextColored(kRed, "Carve job not initialized.");
        return;
    }

    if (!m_modelLoaded) {
        ImGui::TextColored(kYellow, "Load an STL model first (go back to Model step).");
        return;
    }

    auto jobState = m_carveJob->state();
    float bw = ImGui::GetFontSize() * 14.0f;
    bool hmReady = (jobState == carve::CarveJobState::Ready);
    bool hmComputing = (jobState == carve::CarveJobState::Computing);

    // Auto-load or auto-compute heightmap on first entry to Preview
    if (jobState == carve::CarveJobState::Idle && !m_hmInitAttempted && !m_hmFileMissing) {
        m_hmInitAttempted = true;
        m_fitter.setStock(m_stock);

        // Check project manifest for cached heightmap
        bool loaded = false;
        if (m_projectManager) {
            auto dir = m_projectManager->ensureProjectForModel(m_modelName, m_modelSourcePath);
            if (dir && !dir->heightmaps().empty()) {
                const auto& entry = dir->heightmaps().front();
                Path fullPath = dir->heightmapsDir() / entry.filename;
                if (fs::exists(fullPath)) {
                    if (m_carveJob->loadHeightmap(fullPath.string())) {
                        loaded = true;
                        m_heightmapSaved = true; // Already on disk
                        ToastManager::instance().show(ToastType::Success,
                            "Heightmap Loaded", "Loaded cached heightmap from project");
                    }
                } else {
                    m_hmFileMissing = true;
                    m_hmMissingPath = fullPath.string();
                }
            }
        }

        // First time — no cache found, auto-compute
        if (!loaded && !m_hmFileMissing) {
            carve::HeightmapConfig hmCfg;
            m_carveJob->startHeightmap(m_vertices, m_indices, m_fitter,
                                        m_fitParams, hmCfg);
            hmComputing = true;
        }

        // Refresh state after potential load
        jobState = m_carveJob->state();
        hmReady = (jobState == carve::CarveJobState::Ready);
    }

    // Missing file recovery UI
    if (m_hmFileMissing) {
        ImGui::TextColored(kRed, "Cached heightmap file not found:");
        ImGui::TextWrapped("%s", m_hmMissingPath.c_str());
        ImGui::Spacing();

        if (ImGui::Button("Regenerate", ImVec2(bw, 0))) {
            ImGui::OpenPopup("Confirm Regenerate##missing");
        }
        ImGui::SameLine();
        if (ImGui::Button("Locate...", ImVec2(bw, 0))) {
            if (m_fileDialog) {
                m_fileDialog->showOpen(
                    "Locate Heightmap",
                    {{"Heightmap", "*.dwhm"}},
                    [this](const std::string& path) {
                        if (m_carveJob->loadHeightmap(path)) {
                            m_hmFileMissing = false;
                            m_heightmapSaved = true;
                            ToastManager::instance().show(ToastType::Success,
                                "Heightmap Loaded", path);
                        } else {
                            ToastManager::instance().show(ToastType::Error,
                                "Load Failed", "Could not read " + path);
                        }
                    });
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(bw, 0))) {
            m_hmFileMissing = false;
        }

        if (ImGui::BeginPopupModal("Confirm Regenerate##missing", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("This will recompute the heightmap from the mesh. This may take a while.");
            ImGui::Spacing();
            if (ImGui::Button("Continue", ImVec2(bw, 0))) {
                m_hmFileMissing = false;
                m_heightmapSaved = false;
                if (m_hmPreviewTex != 0) { glDeleteTextures(1, &m_hmPreviewTex); m_hmPreviewTex = 0; }
                m_fitter.setStock(m_stock);
                carve::HeightmapConfig hmCfg;
                m_carveJob->startHeightmap(m_vertices, m_indices, m_fitter,
                                            m_fitParams, hmCfg);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel##regen", ImVec2(bw, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        return;
    }

    // Step 1: Heightmap
    {
        if (hmComputing) {
            ImGui::TextColored(kYellow, "1. Computing heightmap...");
            CenteredProgressBar(m_carveJob->progress(), ImVec2(-1, 0), "Computing heightmap...");
        } else if (jobState == carve::CarveJobState::Error) {
            ImGui::TextColored(kRed, "1. Heightmap error: %s", m_carveJob->errorMessage().c_str());
            if (ImGui::Button("Retry", ImVec2(bw, 0))) {
                m_fitter.setStock(m_stock);
                carve::HeightmapConfig hmCfg;
                m_carveJob->startHeightmap(m_vertices, m_indices, m_fitter,
                                            m_fitParams, hmCfg);
            }
        } else if (hmReady) {
            // Auto-save after first computation
            if (!m_heightmapSaved) {
                saveHeightmapToProject();
                m_heightmapSaved = true;
            }

            // Upload preview texture once when heightmap becomes ready
            if (m_hmPreviewTex == 0)
                uploadHeightmapPreview();

            const auto& hm = m_carveJob->heightmap();
            ImGui::TextColored(kGreen, "1. Heightmap: Ready");
            ImGui::SameLine();
            ImGui::TextDisabled("(%dx%d, %.2f mm/px)",
                                hm.cols(),
                                hm.rows(),
                                static_cast<double>(hm.resolution()));

            // Heightmap preview image (fit to available width, preserve aspect)
            if (m_hmPreviewTex != 0 && m_hmPreviewW > 0 && m_hmPreviewH > 0) {
                f32 availW = ImGui::GetContentRegionAvail().x;
                f32 aspect = static_cast<f32>(m_hmPreviewH) / static_cast<f32>(m_hmPreviewW);
                f32 dispW = std::min(availW, static_cast<f32>(m_hmPreviewW));
                f32 dispH = dispW * aspect;
                ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(m_hmPreviewTex)),
                             ImVec2(dispW, dispH));
            }

            // Export + Regenerate buttons
            if (ImGui::Button("Export Image")) saveImageToProject();
            ImGui::SameLine();
            if (ImGui::Button("Regenerate Heightmap")) {
                ImGui::OpenPopup("Confirm Regenerate");
            }

            if (ImGui::BeginPopupModal("Confirm Regenerate", nullptr,
                                        ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped(
                    "This will recompute the heightmap and overwrite the cached version. "
                    "This may take a while. Continue?");
                ImGui::Spacing();
                if (ImGui::Button("Continue", ImVec2(bw, 0))) {
                    m_heightmapSaved = false;
                    m_toolpathGenerated = false;  // Hard reset: heightmap changed
                    ++m_settingsVersion;
                    if (m_hmPreviewTex != 0) { glDeleteTextures(1, &m_hmPreviewTex); m_hmPreviewTex = 0; }
                    m_fitter.setStock(m_stock);
                    carve::HeightmapConfig hmCfg;
                    m_carveJob->startHeightmap(m_vertices, m_indices, m_fitter,
                                                m_fitParams, hmCfg);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel##regen2", ImVec2(bw, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
    }

    // Step 2: Toolpath (only after heightmap)
    ImGui::Spacing();
    bool toolpathStale = m_toolpathGenerated
                         && (m_generatedAtVersion != m_settingsVersion);
    {
        ImVec4 tpColor = kDimmed;
        const char* tpLabel = "2. Toolpath: Not generated";
        if (m_toolpathGenerated && !toolpathStale) {
            tpColor = kGreen;
            tpLabel = "2. Toolpath: Generated";
        } else if (m_toolpathGenerated && toolpathStale) {
            tpColor = kYellow;
            tpLabel = "2. Toolpath: Settings changed";
        } else if (hmReady) {
            tpColor = kYellow;
        }
        ImGui::TextColored(tpColor, "%s", tpLabel);

        if (hmReady && (!m_toolpathGenerated || toolpathStale)) {
            const char* btnLabel = toolpathStale
                ? "Regenerate Toolpath" : "Generate Toolpath";
            if (ImGui::Button(btnLabel, ImVec2(bw, 0))) {
                f32 toolAngle = static_cast<f32>(m_finishTool.included_angle);
                if (toolAngle <= 0.0f) toolAngle = 90.0f;
                if (!m_recommendationRun)
                    m_carveJob->analyzeHeightmap(toolAngle);

                const VtdbToolGeometry* clrPtr =
                    m_clearToolSelected ? &m_clearTool : nullptr;
                m_carveJob->generateToolpath(m_toolpathConfig, m_finishTool, clrPtr);
                m_toolpathGenerated = true;
                m_generatedAtVersion = m_settingsVersion;
            }
        }
    }

    if (!m_toolpathGenerated) return;

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    const auto& tp = m_carveJob->toolpath();
    const auto& hm = m_carveJob->heightmap();

    // Preview area sized to heightmap aspect ratio
    float panelW = ImGui::GetContentRegionAvail().x;
    float aspect = (hm.cols() > 0 && hm.rows() > 0)
        ? static_cast<f32>(hm.cols()) / static_cast<f32>(hm.rows()) : 1.0f;
    float imgW = panelW * m_previewZoom;
    float imgH = imgW / aspect;

    ImVec2 imgPos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(imgW, imgH));

    // Toolpath overlay via ImDrawList
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Vec3 hmMin = hm.boundsMin();
    Vec3 hmMax = hm.boundsMax();
    f32 rx = hmMax.x - hmMin.x;
    f32 ry = hmMax.y - hmMin.y;

    auto toScreen = [&](const Vec3& p) -> ImVec2 {
        f32 nx = (rx > 0.0f) ? (p.x - hmMin.x) / rx : 0.5f;
        f32 ny = (ry > 0.0f) ? (p.y - hmMin.y) / ry : 0.5f;
        return ImVec2(imgPos.x + nx * imgW, imgPos.y + (1.0f - ny) * imgH);
    };

    if (m_showFinishing && tp.finishing.points.size() > 1) {
        for (size_t i = 1; i < tp.finishing.points.size(); ++i) {
            ImU32 c = tp.finishing.points[i].rapid ? kRapidColor : kFinishColor;
            dl->AddLine(toScreen(tp.finishing.points[i-1].position),
                        toScreen(tp.finishing.points[i].position), c, 1.0f);
        }
    }
    if (m_showClearing && tp.clearing.points.size() > 1) {
        for (size_t i = 1; i < tp.clearing.points.size(); ++i) {
            ImU32 c = tp.clearing.points[i].rapid ? kRapidColor : kClearColor;
            dl->AddLine(toScreen(tp.clearing.points[i-1].position),
                        toScreen(tp.clearing.points[i].position), c, 1.0f);
        }
    }

    // Statistics
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::Text("Finishing: %d scan passes, %s, %.0f mm",
                tp.finishing.scanLineCount,
                formatTime(tp.finishing.estimatedTimeSec).c_str(),
                static_cast<double>(tp.finishing.totalDistanceMm));
    ImGui::TextDisabled("  G-code lines: %d", tp.finishing.lineCount);
    if (!tp.clearing.points.empty()) {
        ImGui::Text("Clearing:  %d scan passes, %s, %.0f mm",
                    tp.clearing.scanLineCount,
                    formatTime(tp.clearing.estimatedTimeSec).c_str(),
                    static_cast<double>(tp.clearing.totalDistanceMm));
        ImGui::TextDisabled("  G-code lines: %d", tp.clearing.lineCount);
    }
    ImGui::Text("Total estimated time: %s", formatTime(tp.totalTimeSec).c_str());

    // Controls
    ImGui::Spacing();
    ImGui::Checkbox("Show finishing", &m_showFinishing);
    ImGui::SameLine();
    ImGui::Checkbox("Show clearing", &m_showClearing);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    ImGui::SliderFloat("Zoom", &m_previewZoom, 0.25f, 4.0f, "%.1fx");

    // G-code export
    ImGui::Spacing();
    if (ImGui::Button("Save G-code", ImVec2(bw, 0)))
        saveGCodeToProject();
}

// --- renderOutlineTest (18-02): Bounding box, G-code outline, skip ---

void DirectCarvePanel::renderOutlineTest() {
    ImGui::TextUnformatted("Outline Test");
    ImGui::Spacing();
    ImGui::TextWrapped("Traces the job perimeter at safe Z to verify work area.");
    ImGui::Spacing();

    if (!m_carveJob || !m_toolpathGenerated) {
        ImGui::TextColored(kYellow, "Generate a toolpath first.");
        return;
    }

    const auto& tp = m_carveJob->toolpath();
    f32 minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (const auto& pt : tp.finishing.points) {
        if (!pt.rapid) {
            minX = std::min(minX, pt.position.x);
            maxX = std::max(maxX, pt.position.x);
            minY = std::min(minY, pt.position.y);
            maxY = std::max(maxY, pt.position.y);
        }
    }

    ImGui::Text("Bounding box: X[%.1f .. %.1f]  Y[%.1f .. %.1f]",
                static_cast<double>(minX), static_cast<double>(maxX),
                static_cast<double>(minY), static_cast<double>(maxY));
    ImGui::Text("Size: %.1f x %.1f mm",
                static_cast<double>(maxX - minX), static_cast<double>(maxY - minY));
    ImGui::Spacing();

    float bw = ImGui::GetFontSize() * 10.0f;

    if (!m_outlineCompleted && !m_outlineRunning) {
        if (ImGui::Button("Run Outline", ImVec2(bw, 0))) {
            if (m_cnc && m_cncConnected) {
                f32 safeZ = m_toolpathConfig.safeZMm;
                char cmd[128];
                std::snprintf(cmd, sizeof(cmd), "G90 G0 Z%.3f", static_cast<double>(safeZ));
                m_cnc->sendCommand(cmd);
                std::snprintf(cmd, sizeof(cmd), "G0 X%.3f Y%.3f",
                              static_cast<double>(minX), static_cast<double>(minY));
                m_cnc->sendCommand(cmd);
                std::snprintf(cmd, sizeof(cmd), "G0 X%.3f Y%.3f",
                              static_cast<double>(maxX), static_cast<double>(minY));
                m_cnc->sendCommand(cmd);
                std::snprintf(cmd, sizeof(cmd), "G0 X%.3f Y%.3f",
                              static_cast<double>(maxX), static_cast<double>(maxY));
                m_cnc->sendCommand(cmd);
                std::snprintf(cmd, sizeof(cmd), "G0 X%.3f Y%.3f",
                              static_cast<double>(minX), static_cast<double>(maxY));
                m_cnc->sendCommand(cmd);
                std::snprintf(cmd, sizeof(cmd), "G0 X%.3f Y%.3f",
                              static_cast<double>(minX), static_cast<double>(minY));
                m_cnc->sendCommand(cmd);
                m_outlineCompleted = true;
            }
        }
        ImGui::SameLine();
        ImGui::Checkbox("Skip Outline", &m_outlineSkipped);
    }

    if (m_outlineCompleted)
        ImGui::TextColored(kGreen, "Outline complete -- verify work area before proceeding.");
    if (m_outlineSkipped && !m_outlineCompleted)
        ImGui::TextColored(kYellow, "Outline test skipped.");
}

// Two-pass probe on a single axis: fast seek, retract, slow accurate pass.
// All commands are incremental (caller must set G91 beforehand).
void DirectCarvePanel::sendProbeAxis(char axis, f32 direction, f32 searchDist,
                                     f32 fastSpeed, f32 slowSpeed, f32 retractDist) {
    char cmd[256];
    f32 sign = (direction >= 0.0f) ? 1.0f : -1.0f;

    // Fast seek
    std::snprintf(cmd, sizeof(cmd), "G38.2 %c%.1f F%.0f",
                  axis, static_cast<double>(sign * searchDist),
                  static_cast<double>(fastSpeed));
    m_cnc->sendCommand(cmd);

    // Retract
    std::snprintf(cmd, sizeof(cmd), "G0 %c%.1f",
                  axis, static_cast<double>(-sign * retractDist));
    m_cnc->sendCommand(cmd);

    // Slow accurate pass
    std::snprintf(cmd, sizeof(cmd), "G38.2 %c%.1f F%.0f",
                  axis, static_cast<double>(sign * (retractDist + 1.0f)),
                  static_cast<double>(slowSpeed));
    m_cnc->sendCommand(cmd);

    // Dwell for position settle
    m_cnc->sendCommand("G4 P0.15");
}

// Probe Z and set WCS Z = plateThickness (zeros Z at stock surface below plate).
void DirectCarvePanel::sendProbeZ(f32 plateThickness) {
    char cmd[256];
    sendProbeAxis('Z', -1.0f, m_probeSearchDist, m_probeFastSpeed,
                  m_probeSlowSpeed, m_probeRetractDist);

    std::snprintf(cmd, sizeof(cmd), "G10 L20 P0 Z%.3f",
                  static_cast<double>(plateThickness));
    m_cnc->sendCommand(cmd);

    // Retract above plate
    std::snprintf(cmd, sizeof(cmd), "G0 Z%.1f",
                  static_cast<double>(m_probeRetractDist));
    m_cnc->sendCommand(cmd);
}

// Probe an XY axis, set WCS offset accounting for plate wall + tool radius.
void DirectCarvePanel::sendProbeXY(char axis, f32 direction,
                                   f32 xyThickness, f32 toolRadius) {
    char cmd[256];
    f32 sign = (direction >= 0.0f) ? 1.0f : -1.0f;
    sendProbeAxis(axis, direction, m_probeSearchDist, m_probeFastSpeed,
                  m_probeSlowSpeed, m_probeRetractDist);

    // Set axis = -(xyThickness + toolRadius) * sign
    // Contact point is at the plate outer face; work zero is at the
    // stock edge (inset by wall thickness + tool radius from contact).
    f32 offset = -sign * (xyThickness + toolRadius);
    std::snprintf(cmd, sizeof(cmd), "G10 L20 P0 %c%.3f",
                  axis, static_cast<double>(offset));
    m_cnc->sendCommand(cmd);

    // Retract away from edge
    std::snprintf(cmd, sizeof(cmd), "G0 %c%.1f",
                  axis, static_cast<double>(-sign * m_probeRetractDist));
    m_cnc->sendCommand(cmd);
}

void DirectCarvePanel::renderZeroConfirm() {
    ImGui::TextUnformatted("Zero Position Confirmation");
    ImGui::Spacing();

    ImGui::Text("Current Work Position:");
    ImGui::Indent();
    ImGui::Text("X: %.3f  Y: %.3f  Z: %.3f",
                static_cast<double>(m_machineStatus.workPos.x),
                static_cast<double>(m_machineStatus.workPos.y),
                static_cast<double>(m_machineStatus.workPos.z));
    ImGui::Unindent();

    bool nearZero = (std::fabs(m_machineStatus.workPos.x) < 0.5f &&
                     std::fabs(m_machineStatus.workPos.y) < 0.5f &&
                     std::fabs(m_machineStatus.workPos.z) < 0.5f);
    if (nearZero) ImGui::TextColored(kGreen, "Position is near zero origin.");

    ImGui::Spacing();

    bool canSend = (m_cnc != nullptr && m_cncConnected);
    bool isIdle = canSend && m_machineStatus.state == MachineState::Idle;

    // --- Manual Zero ---
    if (ImGui::CollapsingHeader("Manual Zero", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        ImGui::TextWrapped("Position the tool at work zero "
                           "(bottom-left of stock, Z on top surface).");
        ImGui::Spacing();

        float bw = ImGui::GetFontSize() * 10.0f;
        if (!canSend) ImGui::BeginDisabled();
        if (ImGui::Button("Set Zero Here", ImVec2(bw, 0)))
            m_cnc->sendCommand("G10 L20 P0 X0 Y0 Z0");
        ImGui::SameLine();
        if (ImGui::Button("Zero XY Only", ImVec2(bw, 0)))
            m_cnc->sendCommand("G10 L20 P0 X0 Y0");
        ImGui::SameLine();
        if (ImGui::Button("Zero Z Only", ImVec2(bw, 0)))
            m_cnc->sendCommand("G10 L20 P0 Z0");
        if (!canSend) ImGui::EndDisabled();
        ImGui::Unindent();
    }

    ImGui::Spacing();

    // --- Touch Plate Probe ---
    if (ImGui::CollapsingHeader("Touch Plate Probe", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        // Probe pin indicator
        bool probeActive = (m_machineStatus.inputPins & cnc::PIN_PROBE) != 0;
        ImVec4 probeColor = probeActive
            ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
            : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        ImGui::TextColored(probeColor, "Probe pin: %s",
                           probeActive ? "ACTIVE (circuit closed)" : "inactive");
        ImGui::Spacing();

        // --- Mode selector ---
        const char* modeLabels[] = {
            "Z Only", "X Only", "Y Only", "XY Corner", "XYZ Auto"
        };
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        int modeInt = static_cast<int>(m_probeMode);
        if (ImGui::Combo("Probe Mode", &modeInt, modeLabels, 5))
            m_probeMode = static_cast<ProbeMode>(modeInt);

        bool needsXY = (m_probeMode == ProbeMode::XOnly ||
                        m_probeMode == ProbeMode::YOnly ||
                        m_probeMode == ProbeMode::XYCorner ||
                        m_probeMode == ProbeMode::XYZAuto);

        // Corner direction (for XY modes)
        if (needsXY) {
            const char* cornerLabels[] = {
                "Bottom-Left (probe -X, -Y)",
                "Bottom-Right (probe +X, -Y)",
                "Top-Right (probe +X, +Y)",
                "Top-Left (probe -X, +Y)"
            };
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
            ImGui::Combo("Probe Direction", &m_probeCorner, cornerLabels, 4);
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Parameters");

        float fieldW = ImGui::GetFontSize() * 8.0f;

        // Z thickness (for Z and XYZ modes)
        bool needsZ = (m_probeMode == ProbeMode::ZOnly ||
                       m_probeMode == ProbeMode::XYZAuto);
        if (needsZ) {
            ImGui::SetNextItemWidth(fieldW);
            ImGui::InputFloat("Z Plate Thickness (mm)", &m_probeZThickness, 0.5f, 1.0f, "%.2f");
            m_probeZThickness = std::max(0.0f, m_probeZThickness);
        }

        // XY wall thickness (for XY modes)
        if (needsXY) {
            ImGui::SetNextItemWidth(fieldW);
            ImGui::InputFloat("XY Wall Thickness (mm)", &m_probeXYThickness, 0.5f, 1.0f, "%.2f");
            m_probeXYThickness = std::max(0.0f, m_probeXYThickness);
        }

        // Tool diameter (auto-populated from selected finishing tool)
        if (needsXY) {
            if (m_probeToolDiameter <= 0.0f && m_finishingToolSelected)
                m_probeToolDiameter = static_cast<f32>(m_finishTool.diameter);

            ImGui::SetNextItemWidth(fieldW);
            ImGui::InputFloat("Tool Diameter (mm)", &m_probeToolDiameter, 0.1f, 1.0f, "%.3f");
            m_probeToolDiameter = std::max(0.0f, m_probeToolDiameter);
            if (m_finishingToolSelected) {
                ImGui::SameLine();
                ImGui::TextDisabled("(from %s)", m_finishTool.name_format.c_str());
            }
        }

        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputFloat("Fast Speed (mm/min)", &m_probeFastSpeed, 10, 50, "%.0f");
        m_probeFastSpeed = std::clamp(m_probeFastSpeed, 10.0f, 1000.0f);

        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputFloat("Slow Speed (mm/min)", &m_probeSlowSpeed, 5, 25, "%.0f");
        m_probeSlowSpeed = std::clamp(m_probeSlowSpeed, 5.0f, 500.0f);

        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputFloat("Search Distance (mm)", &m_probeSearchDist, 5, 10, "%.1f");
        m_probeSearchDist = std::clamp(m_probeSearchDist, 1.0f, 200.0f);

        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputFloat("Retract (mm)", &m_probeRetractDist, 0.5f, 1, "%.1f");
        m_probeRetractDist = std::clamp(m_probeRetractDist, 0.1f, 20.0f);

        ImGui::Spacing();

        // Direction signs based on corner
        // BL=0: probe toward -X,-Y  BR=1: +X,-Y  TR=2: +X,+Y  TL=3: -X,+Y
        f32 xDir = (m_probeCorner == 0 || m_probeCorner == 3) ? -1.0f : 1.0f;
        f32 yDir = (m_probeCorner == 0 || m_probeCorner == 1) ? -1.0f : 1.0f;
        f32 toolR = m_probeToolDiameter * 0.5f;

        // --- Command preview ---
        ImGui::SeparatorText("Probe sequence");

        ImGui::TextDisabled("G21 G91  (metric, incremental)");

        switch (m_probeMode) {
        case ProbeMode::ZOnly:
            ImGui::TextDisabled("G38.2 Z-%.1f F%.0f  (fast)",
                                static_cast<double>(m_probeSearchDist),
                                static_cast<double>(m_probeFastSpeed));
            ImGui::TextDisabled("G0 Z%.1f  (retract)",
                                static_cast<double>(m_probeRetractDist));
            ImGui::TextDisabled("G38.2 Z-%.1f F%.0f  (slow)",
                                static_cast<double>(m_probeRetractDist + 1.0f),
                                static_cast<double>(m_probeSlowSpeed));
            ImGui::TextDisabled("G10 L20 P0 Z%.2f",
                                static_cast<double>(m_probeZThickness));
            break;

        case ProbeMode::XOnly:
            ImGui::TextDisabled("G38.2 X%.1f F%.0f  (fast)",
                                static_cast<double>(xDir * m_probeSearchDist),
                                static_cast<double>(m_probeFastSpeed));
            ImGui::TextDisabled("G10 L20 P0 X%.3f  (wall + tool radius)",
                                static_cast<double>(-xDir * (m_probeXYThickness + toolR)));
            break;

        case ProbeMode::YOnly:
            ImGui::TextDisabled("G38.2 Y%.1f F%.0f  (fast)",
                                static_cast<double>(yDir * m_probeSearchDist),
                                static_cast<double>(m_probeFastSpeed));
            ImGui::TextDisabled("G10 L20 P0 Y%.3f  (wall + tool radius)",
                                static_cast<double>(-yDir * (m_probeXYThickness + toolR)));
            break;

        case ProbeMode::XYCorner:
            ImGui::TextDisabled("Probe X -> set X offset");
            ImGui::TextDisabled("Probe Y -> set Y offset");
            ImGui::TextDisabled("Compensation: wall(%.1f) + radius(%.2f) = %.2f mm",
                                static_cast<double>(m_probeXYThickness),
                                static_cast<double>(toolR),
                                static_cast<double>(m_probeXYThickness + toolR));
            break;

        case ProbeMode::XYZAuto:
            ImGui::TextDisabled("1. Probe Z on top of block -> set Z");
            ImGui::TextDisabled("2. Move past X edge, drop Z, probe X -> set X");
            ImGui::TextDisabled("3. Move past Y edge, probe Y -> set Y");
            ImGui::TextDisabled("4. Return to work zero");
            ImGui::TextDisabled("Compensation: wall(%.1f) + radius(%.2f) = %.2f mm",
                                static_cast<double>(m_probeXYThickness),
                                static_cast<double>(toolR),
                                static_cast<double>(m_probeXYThickness + toolR));
            break;
        }

        ImGui::TextDisabled("G90  (restore absolute)");

        ImGui::Spacing();

        // --- Run Probe ---
        bool canProbe = isIdle;
        if (!canProbe) ImGui::BeginDisabled();

        const char* probeLabel = "Run Probe";
        float probeW = ImGui::CalcTextSize(probeLabel).x
                       + ImGui::GetStyle().FramePadding.x * 4;
        float probeH = ImGui::GetFrameHeight() * 1.5f;

        if (ImGui::Button(probeLabel, ImVec2(probeW, probeH))) {
            char cmd[256];
            m_cnc->sendCommand("G21 G91");

            switch (m_probeMode) {
            case ProbeMode::ZOnly:
                sendProbeZ(m_probeZThickness);
                break;

            case ProbeMode::XOnly:
                sendProbeXY('X', xDir, m_probeXYThickness, toolR);
                break;

            case ProbeMode::YOnly:
                sendProbeXY('Y', yDir, m_probeXYThickness, toolR);
                break;

            case ProbeMode::XYCorner:
                sendProbeXY('X', xDir, m_probeXYThickness, toolR);
                sendProbeXY('Y', yDir, m_probeXYThickness, toolR);
                break;

            case ProbeMode::XYZAuto: {
                // Phase 1: Probe Z on top of block
                sendProbeZ(m_probeZThickness);

                // Phase 2: Move out past X edge, drop beside block, probe X
                f32 clearance = m_probeXYThickness + m_probeRetractDist + toolR + 6.0f;
                std::snprintf(cmd, sizeof(cmd), "G0 %c%.1f",
                              'X', static_cast<double>(-xDir * clearance));
                m_cnc->sendCommand(cmd);

                f32 zDrop = m_probeZThickness + m_probeRetractDist + 2.0f;
                std::snprintf(cmd, sizeof(cmd), "G0 Z-%.1f",
                              static_cast<double>(zDrop));
                m_cnc->sendCommand(cmd);

                sendProbeXY('X', xDir, m_probeXYThickness, toolR);

                // Phase 3: Move out past Y edge, return X, probe Y
                std::snprintf(cmd, sizeof(cmd), "G0 %c%.1f",
                              'Y', static_cast<double>(-yDir * clearance));
                m_cnc->sendCommand(cmd);
                std::snprintf(cmd, sizeof(cmd), "G0 %c%.1f",
                              'X', static_cast<double>(xDir * clearance));
                m_cnc->sendCommand(cmd);

                sendProbeXY('Y', yDir, m_probeXYThickness, toolR);

                // Phase 4: Raise and return to zero
                std::snprintf(cmd, sizeof(cmd), "G0 Z%.1f",
                              static_cast<double>(zDrop + m_probeRetractDist));
                m_cnc->sendCommand(cmd);

                m_cnc->sendCommand("G90");
                m_cnc->sendCommand("G0 X0 Y0");
                m_cnc->sendCommand("G91");
                break;
            }
            }

            m_cnc->sendCommand("G90");
        }
        if (!canProbe) ImGui::EndDisabled();

        if (!isIdle && canSend) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1),
                               "Machine must be Idle to probe");
        }
        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::Checkbox("Zero position is set and verified", &m_zeroConfirmed);
}

void DirectCarvePanel::renderCommit() {
    ImGui::TextUnformatted("Final Confirmation");
    ImGui::Spacing();
    ImGui::TextWrapped("Review the carve job parameters before starting:");
    ImGui::Spacing();

    ImGui::BulletText("Machine: %s", m_cncConnected ? "Connected" : "DISCONNECTED");
    ImGui::BulletText("Stock: %.0f x %.0f x %.0f mm",
                      static_cast<double>(m_stock.width),
                      static_cast<double>(m_stock.height),
                      static_cast<double>(m_stock.thickness));
    ImGui::BulletText("Feed: %.0f mm/min, Plunge: %.0f mm/min",
                      static_cast<double>(m_toolpathConfig.feedRateMmMin),
                      static_cast<double>(m_toolpathConfig.plungeRateMmMin));
    ImGui::BulletText("Safe Z: %.1f mm", static_cast<double>(m_toolpathConfig.safeZMm));

    if (m_carveJob) {
        const auto& tp = m_carveJob->toolpath();
        ImGui::BulletText("Estimated time: %s", formatTime(tp.totalTimeSec).c_str());
        ImGui::BulletText("G-code lines: %d", tp.totalLineCount);
    }

    ImGui::Spacing();

    // G-code export button
    float bw = ImGui::GetFontSize() * 10.0f;
    if (ImGui::Button("Save as G-code", ImVec2(bw, 0)))
        saveGCodeToProject();

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
    ImGui::TextWrapped("This will begin streaming G-code to the machine. "
                       "Ensure the work area is clear and the spindle is ready.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Checkbox("I confirm the above and am ready to carve", &m_commitConfirmed);
}

void DirectCarvePanel::renderRunning() {
    float fontSize = ImGui::GetFontSize();
    float bw = fontSize * 8.0f;

    // State label
    const char* stateLabel = "Unknown";
    ImVec4 stateColor = kDimmed;
    switch (m_runState) {
    case RunState::Active:
        stateLabel = "Streaming";
        stateColor = kGreen;
        break;
    case RunState::Paused:
        stateLabel = "Paused (Feed Hold)";
        stateColor = kYellow;
        break;
    case RunState::Completed:
        stateLabel = "Complete";
        stateColor = kGreen;
        break;
    case RunState::Aborted:
        stateLabel = "Aborted";
        stateColor = kRed;
        break;
    }

    ImGui::TextColored(stateColor, "%s", stateLabel);
    ImGui::Spacing();

    // Progress bar
    if (m_runTotalLines > 0) {
        float fraction = static_cast<float>(m_runCurrentLine) /
                         static_cast<float>(m_runTotalLines);
        float etaSec = 0.0f;
        if (m_runCurrentLine > 0 && fraction < 1.0f && m_runElapsedSec > 0.0f) {
            float rate = static_cast<float>(m_runCurrentLine) / m_runElapsedSec;
            float remaining = static_cast<float>(m_runTotalLines - m_runCurrentLine);
            etaSec = remaining / rate;
        }
        char overlay[128];
        int etaMin = static_cast<int>(etaSec / 60.0f);
        int etaS = static_cast<int>(etaSec) % 60;
        std::snprintf(overlay, sizeof(overlay), "Line %d / %d  (%.0f%%)  ETA: %d:%02d",
                      m_runCurrentLine, m_runTotalLines,
                      static_cast<double>(fraction * 100.0f), etaMin, etaS);
        CenteredProgressBar(fraction, ImVec2(-1, 0), overlay);
    }

    // Pass indicator and elapsed time
    if (!m_runCurrentPass.empty())
        ImGui::Text("Pass: %s", m_runCurrentPass.c_str());
    ImGui::Text("Elapsed: %s", formatTime(m_runElapsedSec).c_str());

    // Machine position
    ImGui::Text("Position: X%.3f Y%.3f Z%.3f",
                static_cast<double>(m_machineStatus.workPos.x),
                static_cast<double>(m_machineStatus.workPos.y),
                static_cast<double>(m_machineStatus.workPos.z));
    ImGui::Spacing();

    // Control buttons (active job only)
    if (m_runState == RunState::Active || m_runState == RunState::Paused) {
        // Pause / Resume
        if (m_runState == RunState::Active) {
            if (ImGui::Button("Pause", ImVec2(bw, 0))) {
                if (m_cnc) m_cnc->feedHold();
                m_runState = RunState::Paused;
            }
        } else {
            if (ImGui::Button("Resume", ImVec2(bw, 0))) {
                if (m_cnc) m_cnc->cycleStart();
                m_runState = RunState::Active;
            }
        }

        // Abort — long-press for safety
        ImGui::SameLine();
        ImGui::Button("Hold to Abort", ImVec2(bw * 1.2f, 0));
        bool isHeld = ImGui::IsItemActive();
        float requiredMs = 1500.0f;
        if (isHeld) {
            m_abortHoldTime += ImGui::GetIO().DeltaTime * 1000.0f;
            float progress = std::min(m_abortHoldTime / requiredMs, 1.0f);
            ImVec2 rmin = ImGui::GetItemRectMin();
            ImVec2 rmax = ImGui::GetItemRectMax();
            ImVec2 fillMax = {rmin.x + (rmax.x - rmin.x) * progress, rmax.y};
            ImGui::GetWindowDrawList()->AddRectFilled(
                rmin, fillMax, IM_COL32(255, 80, 80, 60), 3.0f);
            m_abortHolding = true;
            if (m_abortHoldTime >= requiredMs) {
                // Execute abort sequence
                if (m_cnc) {
                    m_cnc->feedHold();
                    m_cnc->softReset();
                }
                m_runState = RunState::Aborted;
                if (m_gcodePanel) m_gcodePanel->onCarveStreamAborted();
                m_abortHoldTime = 0.0f;
                m_abortHolding = false;
            }
        } else {
            if (m_abortHolding) { m_abortHolding = false; m_abortHoldTime = 0.0f; }
        }
    }

    // Post-completion / post-abort UI
    if (m_runState == RunState::Completed) {
        ImGui::Spacing();
        ImGui::TextColored(kGreen, "Carve completed successfully.");
        if (ImGui::Button("Save G-code", ImVec2(bw, 0)))
            saveGCodeToProject();
    }

    if (m_runState == RunState::Aborted) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, kRed);
        ImGui::TextWrapped("Job aborted. Tool may be in workpiece -- "
                           "jog Z up before moving XY.");
        ImGui::PopStyleColor();
        if (ImGui::Button("Save G-code", ImVec2(bw, 0)))
            saveGCodeToProject();
    }
}

void DirectCarvePanel::saveHeightmapToProject() {
    if (!m_projectManager || !m_carveJob) {
        // Fallback to FileDialog
        if (m_fileDialog) {
            m_fileDialog->showSave(
                "Save Heightmap",
                {{"Heightmap", "*.dwhm"}},
                "heightmap.dwhm",
                [this](const std::string& path) {
                    m_carveJob->heightmap().save(path);
                });
        }
        return;
    }

    auto dir = m_projectManager->ensureProjectForModel(m_modelName, m_modelSourcePath);
    if (!dir) {
        ToastManager::instance().show(ToastType::Error,
            "Project Error", "Failed to create project directory");
        return;
    }

    std::string baseName = ProjectDirectory::sanitizeName(m_modelName);
    Path destPath = dir->heightmapsDir() / (baseName + ".dwhm");
    const auto& hm = m_carveJob->heightmap();

    if (hm.save(destPath.string())) {
        dir->addHeightmap(baseName + ".dwhm", hm.resolution());
        dir->save();
        ToastManager::instance().show(ToastType::Success,
            "Heightmap Saved", destPath.string());
    } else {
        ToastManager::instance().show(ToastType::Error,
            "Save Failed", "Could not write " + destPath.string());
    }
}

void DirectCarvePanel::saveImageToProject() {
    if (!m_projectManager || !m_carveJob) {
        // Fallback to FileDialog
        if (m_fileDialog) {
            m_fileDialog->showSave(
                "Export Heightmap Image",
                {{"PGM Image (16-bit)", "*.pgm"}},
                "heightmap.pgm",
                [this](const std::string& path) {
                    m_carveJob->heightmap().exportPng(path);
                });
        }
        return;
    }

    auto dir = m_projectManager->ensureProjectForModel(m_modelName, m_modelSourcePath);
    if (!dir) {
        ToastManager::instance().show(ToastType::Error,
            "Project Error", "Failed to create project directory");
        return;
    }

    std::string baseName = ProjectDirectory::sanitizeName(m_modelName);
    Path destPath = dir->imagesDir() / (baseName + ".pgm");

    if (m_carveJob->heightmap().exportPng(destPath.string())) {
        dir->save();
        ToastManager::instance().show(ToastType::Success,
            "Image Exported", destPath.string());
    } else {
        ToastManager::instance().show(ToastType::Error,
            "Export Failed", "Could not write " + destPath.string());
    }
}

void DirectCarvePanel::uploadHeightmapPreview() {
    if (!m_carveJob) return;
    const auto& hm = m_carveJob->heightmap();
    if (hm.empty()) return;

    int w = hm.cols();
    int h = hm.rows();
    f32 range = hm.maxZ() - hm.minZ();
    if (range < 1e-6f) range = 1.0f;

    // Build RGBA pixels (grayscale mapped to a warm depth palette)
    std::vector<u8> pixels(static_cast<size_t>(w * h * 4));
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            f32 z = hm.at(c, r);
            f32 t = std::clamp((z - hm.minZ()) / range, 0.0f, 1.0f);
            // Deep = dark blue, surface = bright white-gold
            u8 rv = static_cast<u8>(std::clamp(t * 255.0f, 0.0f, 255.0f));
            u8 gv = static_cast<u8>(std::clamp(t * 240.0f, 0.0f, 255.0f));
            u8 bv = static_cast<u8>(std::clamp((0.3f + t * 0.7f) * 200.0f, 0.0f, 255.0f));
            size_t idx = static_cast<size_t>((r * w + c) * 4);
            pixels[idx + 0] = rv;
            pixels[idx + 1] = gv;
            pixels[idx + 2] = bv;
            pixels[idx + 3] = 255;
        }
    }

    if (m_hmPreviewTex == 0)
        glGenTextures(1, &m_hmPreviewTex);

    glBindTexture(GL_TEXTURE_2D, m_hmPreviewTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    m_hmPreviewW = w;
    m_hmPreviewH = h;
}

void DirectCarvePanel::saveGCodeToProject() {
    if (!m_projectManager || !m_carveJob) {
        showExportDialog();
        return;
    }

    auto dir = m_projectManager->ensureProjectForModel(m_modelName, m_modelSourcePath);
    if (!dir) {
        ToastManager::instance().show(ToastType::Error,
            "Project Error", "Failed to create project directory");
        showExportDialog();
        return;
    }

    std::string baseName = ProjectDirectory::sanitizeName(m_modelName);
    Path destPath = dir->gcodeDir() / (baseName + ".nc");
    const auto& tp = m_carveJob->toolpath();
    std::string toolName = m_finishTool.name_format;

    if (!carve::exportGcode(destPath.string(), tp, m_toolpathConfig, m_modelName, toolName)) {
        ToastManager::instance().show(ToastType::Error,
            "Export Failed", "Could not write " + destPath.string());
        return;
    }

    dir->addGCode(baseName + ".nc", toolName);
    dir->save();

    std::optional<i64> gcodeId;
    std::optional<GCodeRecord> savedRecord;
    if (m_gcodeRepo) {
        const auto fileHash = hash::computeFile(destPath);
        auto record = makeGeneratedGCodeRecord(destPath, baseName, fileHash);

        auto existing = m_gcodeRepo->findByPath(record.filePath);
        if (!existing) {
            existing = m_gcodeRepo->findByHash(fileHash);
        }

        if (existing) {
            record.id = existing->id;
            if (m_gcodeRepo->update(record)) {
                gcodeId = record.id;
                savedRecord = record;
            }
        } else {
            gcodeId = m_gcodeRepo->insert(record);
            if (gcodeId) {
                record.id = *gcodeId;
                savedRecord = record;
            }
        }

        if (gcodeId && m_projectManager->currentProject()) {
            m_gcodeRepo->addToProject(m_projectManager->currentProject()->id(), *gcodeId);
        }
    }

    if (gcodeId && savedRecord && m_projectManager && m_projectManager->currentProject()) {
        auto operationItemId = syncOperationOpenItem();
        nlohmann::json snapshot = {
            {"hash", savedRecord->hash},
            {"file_path", savedRecord->filePath.string()},
            {"file_size", savedRecord->fileSize},
            {"estimated_time", savedRecord->estimatedTime},
            {"total_distance", savedRecord->totalDistance},
            {"feed_rates", savedRecord->feedRates},
            {"tool_numbers", savedRecord->toolNumbers},
        };

        ProjectOpenItem item;
        item.projectId = m_projectManager->currentProject()->id();
        item.itemType = ProjectOpenItemType::Gcode;
        item.sourceTable = "gcode_files";
        item.sourceId = *gcodeId;
        item.sourceKey = "gcode_files:" + std::to_string(*gcodeId);
        item.parentItemId = operationItemId;
        item.status = ProjectOpenItemStatus::Ready;
        item.displayName = savedRecord->name;
        item.intentJson = R"({"role":"generated_direct_carve_program"})";
        item.snapshotJson = snapshot.dump();
        (void)m_projectManager->upsertOpenItem(std::move(item));
    }

    if (gcodeId && m_libraryManager) {
        std::optional<i64> modelId;
        if (!m_modelSourcePath.empty() && file::exists(m_modelSourcePath)) {
            if (auto model = m_libraryManager->getModelByHash(hash::computeFile(m_modelSourcePath))) {
                modelId = model->id;
            }
        }
        if (!modelId) {
            modelId = m_libraryManager->autoDetectModelMatch(m_modelName);
        }

        if (modelId) {
            auto groups = m_libraryManager->getOperationGroups(*modelId);
            auto groupIt = std::find_if(groups.begin(), groups.end(), [](const OperationGroup& group) {
                return group.name == "Direct Carve";
            });

            std::optional<i64> groupId;
            if (groupIt != groups.end()) {
                groupId = groupIt->id;
            } else {
                groupId = m_libraryManager->createOperationGroup(
                    *modelId, "Direct Carve", static_cast<int>(groups.size()));
            }

            if (groupId) {
                m_libraryManager->addGCodeToGroup(*groupId, *gcodeId);
            }
        }
    }

    if (m_gcodePanel) {
        m_gcodePanel->loadFile(destPath.string());
    }

    ToastManager::instance().show(ToastType::Success,
        "G-code Saved", destPath.string());
}

void DirectCarvePanel::showExportDialog() {
    if (!m_fileDialog || !m_carveJob) return;
    const auto& tp = m_carveJob->toolpath();
    std::string modelName = m_modelName;
    std::string toolName = m_finishTool.name_format;
    carve::ToolpathConfig config = m_toolpathConfig;
    m_fileDialog->showSave("Save G-code",
        {{  "G-code Files", "*.nc;*.gcode;*.ngc"}},
        "carve.nc",
        [tp, config, modelName, toolName](const std::string& path) {
            bool ok = carve::exportGcode(path, tp, config, modelName, toolName);
            if (ok)
                ToastManager::instance().show(ToastType::Success,
                    "G-code Saved", path);
            else
                ToastManager::instance().show(ToastType::Error,
                    "Export Failed", "Could not write " + path);
        });
}

} // namespace dw

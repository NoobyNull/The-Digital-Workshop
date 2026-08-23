// Direct Carve: Carve Preview planning step.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>

#include <imgui.h>

#include "core/carve/carve_job.h"
#include "core/carve/roughing_tool_selector.h"
#include "core/carve/toolpath_advisor.h"
#include "core/config/config.h"
#include "ui/tool_library_access.h"
#include "ui/ui_colors.h"
#include "ui/widgets/toast.h"

namespace dw {

namespace {
constexpr auto& kGreen = colors::kSuccess;
constexpr auto& kRed = colors::kError;
constexpr auto& kYellow = colors::kWarning;
constexpr auto& kDimmed = colors::kDimmed;

// Visualization colors intentionally remain local to the toolpath preview.
constexpr ImU32 kFinishColor = IM_COL32(80, 120, 255, 200);
constexpr ImU32 kClearColor = IM_COL32(235, 155, 65, 190);
constexpr ImU32 kRapidColor = IM_COL32(80, 220, 80, 150);
constexpr ImU32 kPreviewBackground = IM_COL32(22, 26, 31, 255);
constexpr f32 kPreviewStrokeSpacingPixels = 3.0f;
constexpr std::size_t kMaximumPreviewStrokesPerPass = 2000;

f64 tipDiameterMm(const VtdbToolGeometry& geometry) {
    f64 diameter = 0.0;
    if (geometry.flat_diameter > 0.0) {
        diameter = geometry.flat_diameter;
    } else if (geometry.tip_radius > 0.0) {
        diameter = geometry.tip_radius * 2.0;
    } else {
        diameter = geometry.diameter;
    }
    return geometry.units == VtdbUnits::Imperial ? diameter * 25.4 : diameter;
}
} // namespace

void DirectCarvePanel::renderPreview() {
    syncToolpathRapidRateFromProfile();

    if (!renderPreparationStepGuidance(carve_preparation::PreparationStageId::CarvePreview)) {
        ImGui::TextUnformatted("Carve Preview");
        ImGui::TextWrapped(
            "Generate and inspect the planned motion before moving on to machine setup.");
        ImGui::Spacing();
    }

    if (!m_carveJob) {
        ImGui::TextColored(kRed, "Carve job not initialized.");
        return;
    }

    if (!m_modelLoaded) {
        ImGui::TextColored(kYellow, "Load an STL model first (go back to Model step).");
        return;
    }

    float bw = ImGui::GetFontSize() * 14.0f;

    carve::ModelFitter fitter = m_fitter;
    fitter.setStock(m_stock);
    const auto& profile = Config::instance().getActiveMachineProfile();
    fitter.setMachineTravel(profile.maxTravelX, profile.maxTravelY, profile.maxTravelZ);
    const auto fit = fitter.fit(m_fitParams);
    const f32 autoDepth = (m_modelBoundsMax.z - m_modelBoundsMin.z) * m_fitParams.scale;
    const f32 depthMm = std::clamp(m_fitParams.depthMm > 0.0f ? m_fitParams.depthMm : autoDepth,
                                   0.0f,
                                   std::max(m_stock.thickness, 0.0f));

    const auto& finishingIntent = m_toolPlan.finishingIntent();
    if (!finishingIntent) {
        ImGui::TextColored(kYellow, "Select a finishing tool first.");
        return;
    }
    const auto& finishingTool = *finishingIntent;
    if (depthMm <= 0.0f) {
        ImGui::TextColored(kYellow, "Set a cut depth greater than zero.");
        return;
    }

    const Vec3 stockMin{0.0f, 0.0f, 0.0f};
    const Vec3 stockMax{m_stock.width, m_stock.height, 0.0f};
    const auto roughingSelection = carve::selectFixedDepthRoughingTool(m_toolboxTools,
                                                                       finishingTool,
                                                                       stockMin,
                                                                       stockMax,
                                                                       fit.modelMin,
                                                                       fit.modelMax,
                                                                       m_toolpathConfig.cutExtents);
    const auto clearingTool = m_toolPlan.resolveClearingIntent(roughingSelection.tool);
    if (m_toolPlan.clearingMode() == carve::ClearingToolMode::Disabled) {
        ImGui::TextDisabled("Clearing pass: disabled in the tool plan.");
    } else if (m_toolPlan.clearingMode() == carve::ClearingToolMode::Selected && clearingTool) {
        ImGui::TextColored(kGreen,
                           "Selected clearing tool: %s",
                           resolveToolNameFormat(*clearingTool).c_str());
        ImGui::TextDisabled("Why: this tool clears broad stock first; Direct Carve pauses before "
                            "finishing when a change is required.");
    } else if (m_toolPlan.clearingMode() == carve::ClearingToolMode::Selected) {
        ImGui::TextColored(kYellow, "Choose the clearing tool before generating the path.");
    } else if (roughingSelection.tool) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              roughingSelection.requiresToolChange ? kYellow : kGreen);
        ImGui::TextWrapped("Suggested roughing tool: %s",
                           resolveToolNameFormat(*roughingSelection.tool).c_str());
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped(
            "%s",
            roughingSelection.requiresToolChange
                ? "Why: a separate roughing tool clears broad stock faster; change to the "
                  "finishing tool before rastering."
                : "Why: the finishing tool can also clear the stock, avoiding a tool change.");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("No roughing suggestion: %s", roughingSelection.warning.c_str());
        ImGui::PopStyleColor();
    }

    bool toolpathStale = m_toolpathGenerated && (m_generatedAtVersion != m_settingsVersion);
    const bool surfaceModelToolpath = m_toolpathConfig.cutExtents == carve::CutExtents::Model;
    if (m_surfaceToolpathPending) {
        if (m_carveJob->state() == carve::CarveJobState::Computing) {
            ImGui::TextColored(kYellow,
                               "Building model surface map... %.0f%%",
                               static_cast<double>(m_carveJob->progress() * 100.0f));
        } else if (m_carveJob->state() == carve::CarveJobState::Ready) {
            m_carveJob->analyzeHeightmap(static_cast<f32>(finishingTool.included_angle));
            m_carveJob->generateToolpath(m_toolpathConfig,
                                         finishingTool,
                                         clearingTool ? &*clearingTool : nullptr);
            (void)m_toolPlan.confirmEffectiveClearing(
                clearingTool, !m_carveJob->toolpath().clearing.points.empty());
            m_toolpathGenerated = true;
            m_generatedAtVersion = m_surfaceToolpathPendingVersion;
            m_surfaceToolpathPending = false;
            m_surfaceToolpathPendingVersion = -1;
            completePinnedPreviewGeneration(true);
            publishGCode3DPreview();
        } else if (m_carveJob->state() == carve::CarveJobState::Error) {
            m_toolPlan.invalidateEffectiveClearing();
            clearGCode3DPreview();
            m_toolpathGenerated = false;
            m_generatedAtVersion = -1;
            m_surfaceToolpathPending = false;
            m_surfaceToolpathPendingVersion = -1;
            completePinnedPreviewGeneration(false);
            ToastManager::instance().show(ToastType::Error,
                                          "Surface Map Failed",
                                          m_carveJob->errorMessage());
        }
    }
    {
        ImVec4 tpColor = kDimmed;
        const char* tpLabel = "Toolpath: Not generated";
        if (m_toolpathGenerated && !toolpathStale) {
            tpColor = kGreen;
            tpLabel = "Toolpath: Generated";
        } else if (m_toolpathGenerated && toolpathStale) {
            tpColor = kYellow;
            tpLabel = "Toolpath: Settings changed";
        } else {
            tpColor = kYellow;
        }
        ImGui::TextColored(tpColor, "%s", tpLabel);
        ImGui::SameLine();
        ImGui::TextDisabled("Depth %.2f mm, stepdown %.2f mm",
                            static_cast<double>(depthMm),
                            static_cast<double>(m_toolpathConfig.stepdownMm));

        if (!m_toolpathGenerated || toolpathStale) {
            const char* btnLabel = toolpathStale ? "Regenerate Toolpath" : "Generate Toolpath";
            const bool clearingChoiceIncomplete =
                m_toolPlan.clearingMode() == carve::ClearingToolMode::Selected && !clearingTool;
            if (clearingChoiceIncomplete)
                ImGui::BeginDisabled();
            if (ImGui::Button(btnLabel, ImVec2(bw, 0)) && requestPinnedPreviewGeneration()) {
                m_toolPlan.invalidateEffectiveClearing();
                m_autoRoughingWarning = m_toolPlan.clearingMode() ==
                                                carve::ClearingToolMode::Automatic
                                            ? roughingSelection.warning
                                            : std::string{};

                if (surfaceModelToolpath) {
                    if (m_modelVertices.empty() || m_modelIndices.empty()) {
                        ToastManager::instance().show(
                            ToastType::Error,
                            "Toolpath Failed",
                            "Model geometry is not available for surface carving.");
                        m_toolpathGenerated = false;
                        m_generatedAtVersion = -1;
                        m_toolPlan.invalidateEffectiveClearing();
                        completePinnedPreviewGeneration(false);
                    } else {
                        carve::HeightmapConfig hmCfg;
                        hmCfg.resolutionMm = carve::effectiveScanResolutionMm(
                            m_toolpathConfig, static_cast<f32>(tipDiameterMm(finishingTool)), 1.0f);
                        hmCfg.defaultZ = fit.modelMin.z;
                        m_carveJob->startHeightmap(
                            m_modelVertices, m_modelIndices, fitter, m_fitParams, hmCfg);
                        m_surfaceToolpathPending = true;
                        m_surfaceToolpathPendingVersion = m_settingsVersion;
                        m_toolpathGenerated = false;
                        m_generatedAtVersion = -1;
                    }
                } else {
                    m_carveJob->generateFixedDepthToolpath(stockMin,
                                                           stockMax,
                                                           fit.modelMin,
                                                           fit.modelMax,
                                                           depthMm,
                                                           m_toolpathConfig,
                                                           finishingTool,
                                                           clearingTool ? &*clearingTool : nullptr);
                    if (m_carveJob->state() == carve::CarveJobState::Ready) {
                        (void)m_toolPlan.confirmEffectiveClearing(
                            clearingTool, !m_carveJob->toolpath().clearing.points.empty());
                        m_toolpathGenerated = true;
                        m_generatedAtVersion = m_settingsVersion;
                        completePinnedPreviewGeneration(true);
                        publishGCode3DPreview();
                    } else {
                        m_toolPlan.invalidateEffectiveClearing();
                        clearGCode3DPreview();
                        m_toolpathGenerated = false;
                        m_generatedAtVersion = -1;
                        completePinnedPreviewGeneration(false);
                        ToastManager::instance().show(ToastType::Error,
                                                      "Toolpath Failed",
                                                      m_carveJob->errorMessage());
                    }
                }
            }
            if (clearingChoiceIncomplete)
                ImGui::EndDisabled();
        }
    }

    if (!m_toolpathGenerated)
        return;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const auto& tp = m_carveJob->toolpath();

    // Preview area sized to stock so automatic clearing outside the model is visible.
    float panelW = ImGui::GetContentRegionAvail().x;
    const f32 previewMinX = std::min(0.0f, fit.modelMin.x);
    const f32 previewMinY = std::min(0.0f, fit.modelMin.y);
    const f32 previewMaxX = std::max(m_stock.width, fit.modelMax.x);
    const f32 previewMaxY = std::max(m_stock.height, fit.modelMax.y);
    const f32 rx = std::max(previewMaxX - previewMinX, 1.0f);
    const f32 ry = std::max(previewMaxY - previewMinY, 1.0f);
    float aspect = rx / ry;
    float imgW = panelW * m_previewZoom;
    float imgH = imgW / aspect;

    ImVec2 imgPos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(imgW, imgH));

    // The raw path can contain hundreds of thousands of interpolation points.
    // CarveJob caches top-down strokes when it generates the path; select a
    // screen-space subset here so dense rasters remain legible and responsive.
    const auto& preview = m_carveJob->toolpathPreview();
    const f32 pixelsPerMmX = imgW / rx;
    const f32 pixelsPerMmY = imgH / ry;
    const auto clearingSelection =
        carve::selectToolpathPreviewStrokes(preview.clearing,
                                            pixelsPerMmX,
                                            pixelsPerMmY,
                                            kPreviewStrokeSpacingPixels,
                                            kMaximumPreviewStrokesPerPass);
    const auto finishingSelection =
        carve::selectToolpathPreviewStrokes(preview.finishing,
                                            pixelsPerMmX,
                                            pixelsPerMmY,
                                            kPreviewStrokeSpacingPixels,
                                            kMaximumPreviewStrokesPerPass);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto toScreen = [&](const Vec3& p) -> ImVec2 {
        f32 nx = (p.x - previewMinX) / rx;
        f32 ny = (p.y - previewMinY) / ry;
        return ImVec2(imgPos.x + nx * imgW, imgPos.y + (1.0f - ny) * imgH);
    };
    auto addWorldRect = [&](const Vec3& a, const Vec3& b, ImU32 color) {
        const ImVec2 p0 = toScreen(a);
        const ImVec2 p1 = toScreen(b);
        const ImVec2 rmin{std::min(p0.x, p1.x), std::min(p0.y, p1.y)};
        const ImVec2 rmax{std::max(p0.x, p1.x), std::max(p0.y, p1.y)};
        dl->AddRect(rmin, rmax, color, 0.0f, 0, 1.0f);
    };
    auto drawPolyline = [&](const std::vector<Vec3>& points, ImU32 color, f32 thickness) {
        for (std::size_t index = 1; index < points.size(); ++index) {
            dl->AddLine(toScreen(points[index - 1]), toScreen(points[index]), color, thickness);
        }
    };
    auto drawSelectedStrokes = [&](const carve::ToolpathPreviewGeometry& geometry,
                                   const carve::ToolpathPreviewSelection& selection,
                                   ImU32 cutColor) {
        for (const std::size_t strokeIndex : selection.strokeIndices) {
            const auto& stroke = geometry.strokes[strokeIndex];
            drawPolyline(stroke.rapidPoints, kRapidColor, 1.0f);
            drawPolyline(stroke.cutPoints, cutColor, 1.35f);
        }
    };

    const ImVec2 imgMax{imgPos.x + imgW, imgPos.y + imgH};
    dl->PushClipRect(imgPos, imgMax, true);
    dl->AddRectFilled(imgPos, imgMax, kPreviewBackground);

    addWorldRect(Vec3{0.0f, 0.0f, 0.0f},
                 Vec3{m_stock.width, m_stock.height, 0.0f},
                 IM_COL32(140, 150, 160, 180));
    addWorldRect(fit.modelMin, fit.modelMax, IM_COL32(255, 255, 255, 170));

    if (m_showClearing) {
        drawSelectedStrokes(preview.clearing, clearingSelection, kClearColor);
    }

    if (m_showFinishing) {
        drawSelectedStrokes(preview.finishing, finishingSelection, kFinishColor);
    }
    dl->PopClipRect();

    const std::size_t displayedStrokeCount =
        (m_showClearing ? clearingSelection.strokeIndices.size() : 0U) +
        (m_showFinishing ? finishingSelection.strokeIndices.size() : 0U);
    const std::size_t totalStrokeCount =
        (m_showClearing ? clearingSelection.totalStrokeCount : 0U) +
        (m_showFinishing ? finishingSelection.totalStrokeCount : 0U);
    if (displayedStrokeCount < totalStrokeCount) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped(
            "Display simplified for clarity: %zu of %zu cutting strokes at this zoom. "
            "Zoom in to reveal more; every stroke is still used for G-code.",
            displayedStrokeCount,
            totalStrokeCount);
        ImGui::PopStyleColor();
    }

    // Statistics
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Total estimated time: %s", formatTime(tp.totalTimeSec).c_str());
    if (tp.requiresToolChange) {
        ImGui::TextColored(kYellow,
                           "Tool change required before raster: %s",
                           tp.finishingToolName.c_str());
    }

    const auto runtimeAdvice =
        carve::adviseToolpathRuntime(m_toolpathConfig, tp.totalTimeSec, tp.totalLineCount);
    if (runtimeAdvice) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
        ImGui::TextWrapped("Suggested faster path: try %s (%.0f%%), roughly %s. This is optional.",
                           carve::stepoverPresetShortLabel(runtimeAdvice->suggestedPreset),
                           static_cast<double>(runtimeAdvice->suggestedPercent),
                           formatTime(runtimeAdvice->estimatedSeconds).c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled(
            "Why: this carve is long, and ultra-dense rastering usually has diminishing returns.");
    }

    if (ImGui::CollapsingHeader("Advanced preview details")) {
        if (!tp.clearing.points.empty()) {
            ImGui::Text("Clearing: %d scan passes, %s, %.0f mm",
                        tp.clearing.scanLineCount,
                        formatTime(tp.clearing.estimatedTimeSec).c_str(),
                        static_cast<double>(tp.clearing.totalDistanceMm));
            ImGui::TextDisabled("  G-code lines: %d", tp.clearing.lineCount);
        } else {
            ImGui::TextDisabled("Clearing: no clearing motion in this program");
        }
        ImGui::Text("Raster: %d scan passes, %s, %.0f mm",
                    tp.finishing.scanLineCount,
                    formatTime(tp.finishing.estimatedTimeSec).c_str(),
                    static_cast<double>(tp.finishing.totalDistanceMm));
        ImGui::TextDisabled("  G-code lines: %d", tp.finishing.lineCount);

        if (runtimeAdvice && !runtimeAdvice->reachesTarget) {
            ImGui::TextWrapped(
                "Even the coarsest preset remains over the runtime target; a larger tip, "
                "smaller cut area, or shallower carve may be more practical.");
        }

        ImGui::Spacing();
        ImGui::Checkbox("Show clearing", &m_showClearing);
        ImGui::SameLine();
        ImGui::Checkbox("Show raster", &m_showFinishing);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
        ImGui::SliderFloat("Zoom", &m_previewZoom, 0.25f, 4.0f, "%.1fx");
    }

    // The 2D overview is intentionally fast and density-aware. The exact
    // generated program is also available in the application's true 3D
    // viewport without first saving or making it runnable.
    ImGui::Spacing();
    if (!m_open3DPreview)
        ImGui::BeginDisabled();
    if (ImGui::Button("Inspect in 3D", ImVec2(bw, 0)) && m_open3DPreview) {
        publishGCode3DPreview();
        m_open3DPreview();
    }
    if (!m_open3DPreview)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Save G-code", ImVec2(bw, 0)))
        saveGCodeToProject();
}

} // namespace dw

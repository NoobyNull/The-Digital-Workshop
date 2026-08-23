// Direct Carve: Design & Size planning step.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include <imgui.h>

#include "core/config/config.h"
#include "core/materials/material_manager.h"
#include "ui/panels/cut_optimizer_panel.h"
#include "ui/ui_colors.h"
#include "ui/widgets/toast.h"

namespace dw {

namespace {
constexpr auto& kGreen = colors::kSuccess;
constexpr auto& kRed = colors::kError;
constexpr auto& kYellow = colors::kWarning;
constexpr auto& kDimmed = colors::kDimmed;
} // namespace

void DirectCarvePanel::renderModelFit() {
    if (!renderPreparationStepGuidance(
            carve_preparation::PreparationStageId::DesignAndSize)) {
        ImGui::TextUnformatted("Design & Size");
        ImGui::TextWrapped(
            "Fit the design to the real blank you will put on the machine. "
            "Blank size and cut depth are confirmed here before material and tool choices.");
        ImGui::Spacing();
    }

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

    // Material blank dimensions; auto-fit recalculates whenever the blank changes.
    float iw = ImGui::GetFontSize() * 8.0f;
    ImGui::Text("Material Blank:");
    ImGui::PushStyleColor(ImGuiCol_Text, kDimmed);
    ImGui::TextWrapped("Physical material to cut; machine travel is checked separately.");
    ImGui::PopStyleColor();
    auto prevStock = m_stock;
    auto prevFitParams = m_fitParams;
    ImGui::SetNextItemWidth(iw);
    ImGui::InputFloat("Blank width (X) mm", &m_stock.width, 1.0f, 10.0f, "%.1f");
    m_stock.width = std::clamp(m_stock.width, 1.0f, 2000.0f);
    ImGui::SetNextItemWidth(iw);
    ImGui::InputFloat("Blank height (Y) mm", &m_stock.height, 1.0f, 10.0f, "%.1f");
    m_stock.height = std::clamp(m_stock.height, 1.0f, 2000.0f);
    ImGui::SetNextItemWidth(iw);
    ImGui::InputFloat("Blank thickness (Z) mm", &m_stock.thickness, 0.5f, 5.0f, "%.1f");
    m_stock.thickness = std::clamp(m_stock.thickness, 0.5f, 200.0f);
    bool stockChanged = (m_stock.width != prevStock.width || m_stock.height != prevStock.height ||
                         m_stock.thickness != prevStock.thickness);

    float fs = ImGui::GetFontSize();
    float bw = fs * 12.0f;
    if (ImGui::Button("Use Machine Travel", ImVec2(bw, 0))) {
        const auto& prof = Config::instance().getActiveMachineProfile();
        m_stock.width = prof.maxTravelX;
        m_stock.height = prof.maxTravelY;
        m_stock.thickness = prof.maxTravelZ;
        stockChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Copies the active machine travel into the material blank fields.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Edit Machine")) {
        if (m_openMachineProfiles)
            m_openMachineProfiles();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Edit machine travel limits. This does not change the material blank.");
    }

    // Cut list integration
    if (m_cutOptimizer) {
        ImGui::SameLine();
        if (ImGui::Button("Use Cut Part"))
            ImGui::OpenPopup("PickCutListPart");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Copies a cut-list part into the material blank fields.");
        }

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
                    std::snprintf(label,
                                  sizeof(label),
                                  "%s  %.1f x %.1f mm",
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

    // Auto-fit whenever material blank dimensions change.
    if (stockChanged) {
        m_fitter.setStock(m_stock);
        m_fitParams.scale = m_fitter.autoScale();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

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

    const bool fitChanged = m_fitParams.scale != prevFitParams.scale ||
                            m_fitParams.depthMm != prevFitParams.depthMm ||
                            m_fitParams.offsetX != prevFitParams.offsetX ||
                            m_fitParams.offsetY != prevFitParams.offsetY;
    if (stockChanged || fitChanged) {
        markGeometryChanged();
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
    ImGui::Text("Model after transform: %.1f x %.1f x %.1f mm",
                static_cast<double>(dim.x),
                static_cast<double>(dim.y),
                static_cast<double>(dim.z));
    ImGui::TextColored(result.fitsStock ? kGreen : kRed,
                       result.fitsStock ? "Fits blank" : "Exceeds blank");
    ImGui::SameLine();
    ImGui::TextColored(result.fitsMachine ? kGreen : kRed,
                       result.fitsMachine ? "Fits machine travel" : "Exceeds machine travel");
    if (!result.warning.empty())
        ImGui::TextColored(kYellow, "%s", result.warning.c_str());

    // Cut list integration: push carve blank as a cut piece
    if (m_cutOptimizer && m_modelLoaded) {
        ImGui::Spacing();
        if (ImGui::Button("Sync Blank", ImVec2(bw, 0))) {
            syncSetupToOptimizerAndProject();
            char msg[64];
            std::snprintf(msg,
                          sizeof(msg),
                          "Synced %.0fx%.0f mm blank",
                          static_cast<double>(m_stock.width),
                          static_cast<double>(m_stock.height));
            ToastManager::instance().show(ToastType::Success, msg);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Updates the cut optimizer part and project material line.");

        // Show scrap: material blank area vs carve footprint.
        f32 stockArea = m_stock.width * m_stock.height;
        f32 carveArea = dim.x * dim.y;
        if (stockArea > 0.0f && carveArea > 0.0f && carveArea < stockArea) {
            f32 usedPct = (carveArea / stockArea) * 100.0f;
            f32 scrapArea = stockArea - carveArea;
            ImGui::TextDisabled("Blank usage: %.0f%%  (%.0f mm%c scrap)",
                                static_cast<double>(usedPct),
                                static_cast<double>(scrapArea),
                                '\xB2');
        }
    }

    ImGui::EndChild(); // ##controls_col
}

} // namespace dw

#include "cut_optimizer_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#include <imgui.h>

#include "../../core/database/model_repository.h"
#include "../../core/materials/material_manager.h"
#include "../../core/optimizer/cut_list_file.h"
#include "../../core/optimizer/multi_stock_optimizer.h"
#include "../../core/optimizer/waste_breakdown.h"
#include "../../core/project/project.h"
#include "../icons.h"
#include "../ui_colors.h"
#include "../widgets/toast.h"

namespace dw {

CutOptimizerPanel::CutOptimizerPanel() : Panel("Cut Optimizer") {
    m_canvas.zoomMax = 5.0f;
    m_sheetName[0] = '\0';
}

void CutOptimizerPanel::setMaterialManager(MaterialManager* mm) {
    m_materialManager = mm;
    m_materialsLoaded = false;
}

void CutOptimizerPanel::refreshMaterials() {
    if (!m_materialManager)
        return;
    m_materials = m_materialManager->getAllMaterials();
    m_materialsLoaded = true;
}

// ---------------------------------------------------------------------------
// Main render — 3-column layout
// ---------------------------------------------------------------------------
void CutOptimizerPanel::render() {
    if (!m_open)
        return;

    applyMinSize(28, 14);
    if (ImGui::Begin(m_title.c_str(), &m_open)) {
        renderToolbar();
        ImGui::Separator();

        float avail = ImGui::GetContentRegionAvail().x;
        float availH = ImGui::GetContentRegionAvail().y;
        const auto& style = ImGui::GetStyle();
        float spacing = style.ItemSpacing.x;

        // Bottom-up: calculate left column width from content needs
        // Widest row is stock: [input "W"] SameLine [input "H"] — two inputs + two labels + spacing
        float inputW = ImGui::CalcTextSize("00000.00").x + style.FramePadding.x * 2;
        float labelW = ImGui::CalcTextSize("W##sheet").x + spacing;
        float leftContentW = (inputW + labelW) * 2 + spacing;
        float leftW = leftContentW + style.WindowPadding.x * 2;

        // Right column: sized from content — "00pc 100%" + padding
        float rightContentW = ImGui::CalcTextSize("Sheet 0  00pc 100%").x;
        float rightW = rightContentW + style.WindowPadding.x * 2;

        float centerW = avail - leftW - rightW - spacing * 2;

        if (centerW < avail * 0.2f) {
            // Too narrow for 3-column — fall back to stacked
            float stackH = availH * 0.35f;
            float centerH = availH * 0.4f;
            float rightH = availH * 0.2f;

            ImGui::BeginChild("Left", ImVec2(0, stackH), ImGuiChildFlags_Borders);
            renderCutListTable();
            ImGui::Separator();
            renderStockSheets();
            ImGui::Separator();
            renderSettings();
            ImGui::EndChild();

            ImGui::BeginChild("Center", ImVec2(0, centerH), ImGuiChildFlags_Borders);
            if (m_hasResults) {
                renderVisualization();
            } else {
                ImGui::TextDisabled("Add parts and click Optimize");
            }
            ImGui::EndChild();

            if (m_hasResults) {
                ImGui::BeginChild("Right", ImVec2(0, rightH), ImGuiChildFlags_Borders);
                renderLayoutsSidebar();
                ImGui::Separator();
                renderResultsPanel();
                ImGui::EndChild();
            }
        } else {
            // --- 3-column layout ---
            float colH = -ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y;
            // Left column
            ImGui::BeginChild("Left", ImVec2(leftW, colH), ImGuiChildFlags_Borders);
            renderCutListTable();
            ImGui::Separator();
            renderStockSheets();
            ImGui::Separator();
            renderSettings();
            ImGui::EndChild();

            ImGui::SameLine();

            // Center column
            ImGui::BeginChild("Center", ImVec2(centerW, colH), ImGuiChildFlags_Borders);
            if (m_hasResults) {
                // Pagination header
                int sheetCount = static_cast<int>(m_result.sheets.size());
                if (sheetCount > 1) {
                    if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) {
                        m_selectedSheet = std::max(0, m_selectedSheet - 1);
                    }
                    ImGui::SameLine();
                    ImGui::Text("%d / %d", m_selectedSheet + 1, sheetCount);
                    ImGui::SameLine();
                    if (ImGui::ArrowButton("##next", ImGuiDir_Right)) {
                        m_selectedSheet = std::min(sheetCount - 1, m_selectedSheet + 1);
                    }
                    ImGui::Separator();
                }

                renderVisualization();

                // Info line below visualization
                if (m_selectedSheet < static_cast<int>(m_result.sheets.size())) {
                    const auto& sr = m_result.sheets[static_cast<size_t>(m_selectedSheet)];
                    int pieceCount = static_cast<int>(sr.placements.size());
                    float eff = sr.efficiency() * 100.0f;
                    ImGui::Text("%d pieces  %s  %.0fx%.0f  %.1f%%",
                                pieceCount, Icons::Optimizer,
                                static_cast<double>(m_sheet.width),
                                static_cast<double>(m_sheet.height),
                                static_cast<double>(eff));
                }
            } else {
                ImGui::TextDisabled("Add parts and click Optimize");
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Right column
            ImGui::BeginChild("Right", ImVec2(rightW, colH), ImGuiChildFlags_Borders);
            renderLayoutsSidebar();
            ImGui::Separator();
            renderResultsPanel();
            ImGui::EndChild();
        }

        // Status bar at the bottom
        renderStatusBar();
    }
    ImGui::End();

    // Render popups (must be outside Begin/End child)
    renderImportPopup();
}

// ---------------------------------------------------------------------------
// loadCutPlan / clear / saveCutPlan — same logic, minor adjustments
// ---------------------------------------------------------------------------
void CutOptimizerPanel::loadCutPlan(const CutListFile::LoadResult& lr) {
    m_sheet = lr.sheet;
    std::strncpy(m_sheetName, lr.sheet.name.c_str(), sizeof(m_sheetName));
    m_sheetName[sizeof(m_sheetName) - 1] = '\0';
    m_parts = lr.parts;
    m_result = lr.result;
    m_hasResults = !lr.result.sheets.empty();
    m_selectedSheet = 0;
    m_allowRotation = lr.allowRotation;
    m_kerf = lr.kerf;
    m_margin = lr.margin;
    m_algorithm = (lr.algorithm == "first_fit_decreasing")
                      ? optimizer::Algorithm::FirstFitDecreasing
                      : optimizer::Algorithm::Guillotine;
}

void CutOptimizerPanel::addPart(const optimizer::Part& part) {
    m_parts.push_back(part);
    m_hasResults = false;  // Invalidate existing optimization
}

void CutOptimizerPanel::upsertPart(const optimizer::Part& part) {
    auto it = std::find_if(m_parts.begin(), m_parts.end(), [&part](const optimizer::Part& existing) {
        return part.id != 0 && existing.id == part.id;
    });
    if (it == m_parts.end()) {
        it = std::find_if(m_parts.begin(), m_parts.end(), [&part](const optimizer::Part& existing) {
            return existing.name == part.name;
        });
    }

    if (it != m_parts.end()) {
        *it = part;
    } else {
        m_parts.push_back(part);
    }
    m_hasResults = false;
    m_hasMultiResults = false;
}

std::optional<CutOptimizerPanel::StockSelection> CutOptimizerPanel::currentStockSelection() const {
    StockSelection selection;
    selection.sheet = m_sheet;

    if (m_selectedMaterialIdx >= 0 &&
        m_selectedMaterialIdx < static_cast<int>(m_materials.size())) {
        selection.material = m_materials[static_cast<size_t>(m_selectedMaterialIdx)];
    }

    if (m_selectedStockIdx >= 0 &&
        m_selectedStockIdx < static_cast<int>(m_stockSizes.size())) {
        selection.stockSize = m_stockSizes[static_cast<size_t>(m_selectedStockIdx)];
    }

    if (selection.sheet.width <= 0.0f || selection.sheet.height <= 0.0f) {
        return std::nullopt;
    }
    return selection;
}

void CutOptimizerPanel::clear() {
    m_parts.clear();
    m_result = optimizer::CutPlan{};
    m_hasResults = false;
    m_selectedSheet = 0;
    m_editPartIdx = -1;
    m_canvas.reset();
}

void CutOptimizerPanel::saveCutPlan(const char* name) {
    if (!m_cutListFile || !m_hasResults)
        return;

    std::string algorithm = (m_algorithm == optimizer::Algorithm::Guillotine)
                                ? "guillotine"
                                : "first_fit_decreasing";

    if (m_cutListFile->save(name, m_sheet, m_parts, m_result,
                            algorithm, m_allowRotation, m_kerf, m_margin)) {
        ToastManager::instance().show(ToastType::Success, "Saved", "Cut plan saved");
    }
}

std::string CutOptimizerPanel::nextPartLabel() const {
    // A, B, C, ... Z, AA, AB, ...
    int n = static_cast<int>(m_parts.size());
    std::string label;
    do {
        label.insert(label.begin(), static_cast<char>('A' + (n % 26)));
        n = n / 26 - 1;
    } while (n >= 0);
    return label;
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------
void CutOptimizerPanel::renderToolbar() {
    if (ImGui::Button("Optimize"))
        runOptimization();

    ImGui::SameLine();

    // Save
    if (!m_hasResults || !m_cutListFile)
        ImGui::BeginDisabled();
    if (ImGui::Button(Icons::Save))
        ImGui::OpenPopup("Save Cut Plan");
    if (!m_hasResults || !m_cutListFile)
        ImGui::EndDisabled();

    ImGui::SameLine();

    // Load
    if (!m_cutListFile)
        ImGui::BeginDisabled();
    if (ImGui::Button("Load"))
        ImGui::OpenPopup("Load Cut Plan");
    if (!m_cutListFile)
        ImGui::EndDisabled();

    ImGui::SameLine();

    // Import from Project
    bool canImport = m_modelRepo && m_projectManager && m_projectManager->currentProject();
    if (!canImport)
        ImGui::BeginDisabled();
    if (ImGui::Button("Import from Project")) {
        m_importPopupOpen = true;
    }
    if (!canImport)
        ImGui::EndDisabled();

    ImGui::SameLine();

    // Algorithm combo
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("Guillotine__").x + ImGui::GetStyle().FramePadding.x * 2);
    const char* algorithms[] = {"First Fit", "Guillotine"};
    int algoIdx = static_cast<int>(m_algorithm);
    if (ImGui::Combo("##algo", &algoIdx, algorithms, 2))
        m_algorithm = static_cast<optimizer::Algorithm>(algoIdx);

    ImGui::SameLine();

    if (ImGui::Button("Clear"))
        clear();

    // Save popup
    if (ImGui::BeginPopup("Save Cut Plan")) {
        static char planName[128] = {};
        ImGui::InputText("Name", planName, sizeof(planName));
        if (ImGui::Button("Save##confirm") && planName[0] != '\0') {
            saveCutPlan(planName);
            planName[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Load popup
    if (ImGui::BeginPopup("Load Cut Plan")) {
        auto plans = m_cutListFile ? m_cutListFile->list() : std::vector<CutListMeta>{};
        if (plans.empty()) {
            ImGui::TextDisabled("No saved plans");
        } else {
            for (const auto& meta : plans) {
                char label[256];
                std::snprintf(label, sizeof(label), "%s (%d sheets, %d%%)",
                              meta.name.c_str(), meta.sheetsUsed,
                              static_cast<int>(meta.efficiency * 100));
                if (ImGui::Selectable(label)) {
                    auto loaded = m_cutListFile->load(meta.filePath);
                    if (loaded) {
                        loadCutPlan(*loaded);
                        m_loadedPlanPath = meta.filePath;
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// CUT LIST — editable table with inline editing
// ---------------------------------------------------------------------------
void CutOptimizerPanel::renderCutListTable() {
    ImGui::Text("%s CUT LIST", Icons::Model);

    if (m_parts.empty()) {
        ImGui::TextDisabled("No parts added");
    }

    // Cost rate per mm²
    float sheetArea = m_sheet.area();
    float costPerMm2 = (sheetArea > 0.0f && m_sheet.cost > 0.0f) ? m_sheet.cost / sheetArea : 0.0f;

    // Parts table
    if (!m_parts.empty() &&
        ImGui::BeginTable("##parts", 7,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
                          ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 8))) {
        float charW = ImGui::CalcTextSize("0").x;
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("W", ImGuiTableColumnFlags_WidthFixed, charW * 5);
        ImGui::TableSetupColumn("H", ImGuiTableColumnFlags_WidthFixed, charW * 5);
        ImGui::TableSetupColumn("Qty", ImGuiTableColumnFlags_WidthFixed, charW * 3);
        ImGui::TableSetupColumn("$", ImGuiTableColumnFlags_WidthFixed, charW * 5);
        ImGui::TableSetupColumn("R", ImGuiTableColumnFlags_WidthFixed, charW * 2);
        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, charW * 2);
        ImGui::TableHeadersRow();

        int toRemove = -1;
        for (int i = 0; i < static_cast<int>(m_parts.size()); ++i) {
            auto& part = m_parts[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow();

            bool editing = (m_editPartIdx == i);

            // Label
            ImGui::TableNextColumn();
            if (editing) {
                static char editBuf[64];
                if (ImGui::IsWindowAppearing())
                    std::snprintf(editBuf, sizeof(editBuf), "%s", part.name.c_str());
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##lbl", editBuf, sizeof(editBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
                    part.name = editBuf;
                    m_editPartIdx = -1;
                }
            } else {
                ImGui::TextUnformatted(part.name.c_str());
                if (ImGui::IsItemClicked())
                    m_editPartIdx = i;
            }

            // Width
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputFloat("##w", &part.width, 0, 0, "%.0f"))
                m_hasResults = false;

            // Height
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputFloat("##h", &part.height, 0, 0, "%.0f"))
                m_hasResults = false;

            // Quantity
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputInt("##q", &part.quantity, 0)) {
                if (part.quantity < 1) part.quantity = 1;
                m_hasResults = false;
            }

            // Per-part cost
            ImGui::TableNextColumn();
            {
                float partCost = part.area() * static_cast<float>(part.quantity) * costPerMm2;
                ImGui::Text("%.2f", static_cast<double>(partCost));
            }

            // Rotation toggle
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##r", &part.canRotate))
                m_hasResults = false;

            // Delete
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("X"))
                toRemove = i;

            ImGui::PopID();
        }
        ImGui::EndTable();

        if (toRemove >= 0) {
            m_parts.erase(m_parts.begin() + toRemove);
            if (m_editPartIdx == toRemove)
                m_editPartIdx = -1;
            else if (m_editPartIdx > toRemove)
                --m_editPartIdx;
            m_hasResults = false;
        }
    }

    // Add row — divide available width: [W] [H] [Qty] [+Add]
    {
        float availW = ImGui::GetContentRegionAvail().x;
        float btnW = ImGui::CalcTextSize("+ Add").x + ImGui::GetStyle().FramePadding.x * 2;
        float fieldSpace = availW - btnW - ImGui::GetStyle().ItemSpacing.x * 3;
        float dimW = fieldSpace * 0.4f;  // W and H each get 40%
        float qtyW = fieldSpace * 0.2f;  // Qty gets 20%

        ImGui::SetNextItemWidth(dimW);
        ImGui::InputFloat("##nw", &m_newPartWidth, 0, 0, "%.0f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(dimW);
        ImGui::InputFloat("##nh", &m_newPartHeight, 0, 0, "%.0f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(qtyW);
        ImGui::InputInt("##nq", &m_newPartQuantity, 0);
        if (m_newPartQuantity < 1) m_newPartQuantity = 1;
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("+ Add")) {
        optimizer::Part part;
        part.width = m_newPartWidth;
        part.height = m_newPartHeight;
        part.quantity = m_newPartQuantity;
        part.name = m_newPartName[0] ? m_newPartName
                                     : nextPartLabel();
        part.id = static_cast<i64>(m_parts.size());
        m_parts.push_back(part);
        m_newPartName[0] = '\0';
        m_hasResults = false;
    }
}

// ---------------------------------------------------------------------------
// STOCK SHEETS — material dropdown + stock size selection + manual override
// ---------------------------------------------------------------------------
void CutOptimizerPanel::renderStockSheets() {
    ImGui::Text("%s STOCK", Icons::Folder);

    // Load materials on first render
    if (!m_materialsLoaded && m_materialManager) {
        refreshMaterials();
    }

    // Material dropdown
    if (m_materialManager && !m_materials.empty()) {
        const char* previewName = (m_selectedMaterialIdx >= 0 &&
                                    m_selectedMaterialIdx < static_cast<int>(m_materials.size()))
                                       ? m_materials[static_cast<size_t>(m_selectedMaterialIdx)].name.c_str()
                                       : "Select Material...";

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##material", previewName)) {
            // Manual/None option
            if (ImGui::Selectable("(Manual)", m_selectedMaterialIdx == -1)) {
                m_selectedMaterialIdx = -1;
                m_stockSizes.clear();
                m_selectedStockIdx = -1;
            }
            for (int i = 0; i < static_cast<int>(m_materials.size()); ++i) {
                ImGui::PushID(i);
                bool isSelected = (m_selectedMaterialIdx == i);
                if (ImGui::Selectable(m_materials[static_cast<size_t>(i)].name.c_str(), isSelected)) {
                    m_selectedMaterialIdx = i;
                    m_selectedStockIdx = -1;
                    // Load stock sizes for this material
                    m_stockSizes = m_materialManager->getStockSizes(
                        m_materials[static_cast<size_t>(i)].id);
                    // Auto-select first stock size if available
                    if (!m_stockSizes.empty()) {
                        m_selectedStockIdx = 0;
                        const auto& ss = m_stockSizes[0];
                        m_sheet.width = static_cast<f32>(ss.widthMm);
                        m_sheet.height = static_cast<f32>(ss.heightMm);
                        m_sheet.cost = static_cast<f32>(ss.pricePerUnit);
                        m_sheet.name = ss.name.empty()
                                           ? m_materials[static_cast<size_t>(i)].name
                                           : ss.name;
                        std::strncpy(m_sheetName, m_sheet.name.c_str(), sizeof(m_sheetName));
                        m_sheetName[sizeof(m_sheetName) - 1] = '\0';
                        m_hasResults = false;
                    }
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        // Refresh button
        ImGui::SameLine();
        if (ImGui::SmallButton(Icons::Refresh)) {
            refreshMaterials();
        }

        // Stock sizes for selected material
        if (m_selectedMaterialIdx >= 0 && !m_stockSizes.empty()) {
            ImGui::TextDisabled("Stock Sizes:");
            for (int i = 0; i < static_cast<int>(m_stockSizes.size()); ++i) {
                const auto& ss = m_stockSizes[static_cast<size_t>(i)];
                ImGui::PushID(i);
                char label[128];
                std::snprintf(label, sizeof(label), "%s %.0fx%.0f $%.2f/%s",
                              ss.name.empty() ? "Stock" : ss.name.c_str(),
                              ss.widthMm, ss.heightMm,
                              ss.pricePerUnit,
                              ss.unitLabel.empty() ? "sheet" : ss.unitLabel.c_str());
                bool isSelected = (m_selectedStockIdx == i);
                if (ImGui::Selectable(label, isSelected, 0, ImVec2(0, 0))) {
                    m_selectedStockIdx = i;
                    m_sheet.width = static_cast<f32>(ss.widthMm);
                    m_sheet.height = static_cast<f32>(ss.heightMm);
                    m_sheet.cost = static_cast<f32>(ss.pricePerUnit);
                    m_sheet.name = ss.name.empty()
                                       ? m_materials[static_cast<size_t>(m_selectedMaterialIdx)].name
                                       : ss.name;
                    std::strncpy(m_sheetName, m_sheet.name.c_str(), sizeof(m_sheetName));
                    m_sheetName[sizeof(m_sheetName) - 1] = '\0';
                    m_hasResults = false;
                }
                ImGui::PopID();
            }
        } else if (m_selectedMaterialIdx >= 0 && m_stockSizes.empty()) {
            ImGui::TextDisabled("No stock sizes defined for this material");
        }
    } else if (!m_materialManager) {
        ImGui::TextDisabled("Material system not available");
    } else if (m_materials.empty()) {
        ImGui::TextDisabled("No materials in database");
    }

    ImGui::Spacing();

    // Manual sheet override fields (always visible)
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##sheetname", m_sheetName, sizeof(m_sheetName)))
        m_sheet.name = m_sheetName;

    {
        float availW = ImGui::GetContentRegionAvail().x;
        float labelW = ImGui::CalcTextSize("W").x + ImGui::GetStyle().ItemInnerSpacing.x;
        float halfW = (availW - ImGui::GetStyle().ItemSpacing.x) * 0.5f - labelW;
        ImGui::SetNextItemWidth(halfW);
        if (ImGui::InputFloat("W##sheet", &m_sheet.width, 0, 0, "%.0f"))
            m_hasResults = false;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(halfW);
        if (ImGui::InputFloat("H##sheet", &m_sheet.height, 0, 0, "%.0f"))
            m_hasResults = false;
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::InputFloat("$/sheet", &m_sheet.cost, 0, 0, "%.2f");

    // Grain direction indicator
    const char* grainLabel = m_sheet.grainHorizontal ? "Grain: \xe2\x86\x94 Horizontal"
                                                     : "Grain: \xe2\x86\x95 Vertical";
    if (ImGui::Button(grainLabel, ImVec2(-1, 0)))
        m_sheet.grainHorizontal = !m_sheet.grainHorizontal;
}

// ---------------------------------------------------------------------------
// SETTINGS — kerf, padding, Optimize button
// ---------------------------------------------------------------------------
void CutOptimizerPanel::renderSettings() {
    ImGui::Text("%s SETTINGS", Icons::Settings);

    {
        float availW = ImGui::GetContentRegionAvail().x;
        float labelKerf = ImGui::CalcTextSize("Kerf").x + ImGui::GetStyle().ItemInnerSpacing.x;
        float labelPad = ImGui::CalcTextSize("Padding").x + ImGui::GetStyle().ItemInnerSpacing.x;
        float halfW = (availW - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::SetNextItemWidth(halfW - labelKerf);
        if (ImGui::InputFloat("Kerf", &m_kerf, 0, 0, "%.1f"))
            m_hasResults = false;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(halfW - labelPad);
        if (ImGui::InputFloat("Padding", &m_margin, 0, 0, "%.1f"))
            m_hasResults = false;
    }

    ImGui::Checkbox("Allow Rotation", &m_allowRotation);

    // Full-width Optimize button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.50f, 0.33f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.60f, 0.40f, 1.0f));
    if (ImGui::Button("Optimize", ImVec2(-1, ImGui::GetFrameHeight() * 1.3f)))
        runOptimization();
    ImGui::PopStyleColor(2);
}

// ---------------------------------------------------------------------------
// VISUALIZATION — reuse existing Canvas2D
// ---------------------------------------------------------------------------
void CutOptimizerPanel::renderVisualization() {
    if (m_result.sheets.empty())
        return;

    auto area = m_canvas.begin();
    if (!area)
        return;

    // Background
    area.drawList->AddRectFilled(area.pos,
                                 ImVec2(area.pos.x + area.size.x, area.pos.y + area.size.y),
                                 IM_COL32(40, 40, 40, 255));

    // Scale to fit sheet
    float scaleX = (area.size.x - 20) / m_sheet.width;
    float scaleY = (area.size.y - 20) / m_sheet.height;
    float scale = m_canvas.effectiveScale(std::min(scaleX, scaleY));

    float offsetX = area.pos.x + 10 + m_canvas.panX;
    float offsetY = area.pos.y + 10 + m_canvas.panY;

    // Sheet outline
    ImVec2 sheetP1(offsetX, offsetY);
    ImVec2 sheetP2(offsetX + m_sheet.width * scale, offsetY + m_sheet.height * scale);
    area.drawList->AddRectFilled(sheetP1, sheetP2, IM_COL32(60, 60, 60, 255));
    area.drawList->AddRect(sheetP1, sheetP2, IM_COL32(100, 100, 100, 255), 0.0f, 0, 2.0f);

    // Grain direction lines
    {
        ImU32 grainColor = IM_COL32(80, 75, 65, 80);
        float spacing = 12.0f;
        if (m_sheet.grainHorizontal) {
            for (float gy = sheetP1.y + spacing; gy < sheetP2.y; gy += spacing)
                area.drawList->AddLine(ImVec2(sheetP1.x, gy), ImVec2(sheetP2.x, gy), grainColor);
        } else {
            for (float gx = sheetP1.x + spacing; gx < sheetP2.x; gx += spacing)
                area.drawList->AddLine(ImVec2(gx, sheetP1.y), ImVec2(gx, sheetP2.y), grainColor);
        }
    }

    // Draw placements for selected sheet
    if (m_selectedSheet < static_cast<int>(m_result.sheets.size())) {
        const auto& sheetResult = m_result.sheets[static_cast<size_t>(m_selectedSheet)];

        ImU32 colors[] = {
            IM_COL32(66, 133, 244, 200),  // Blue
            IM_COL32(234, 67, 53, 200),   // Red
            IM_COL32(251, 188, 5, 200),   // Yellow
            IM_COL32(52, 168, 83, 200),   // Green
            IM_COL32(171, 71, 188, 200),  // Purple
            IM_COL32(255, 112, 67, 200),  // Orange
            IM_COL32(0, 172, 193, 200),   // Cyan
            IM_COL32(124, 179, 66, 200),  // Lime
        };
        const int numColors = sizeof(colors) / sizeof(colors[0]);

        for (const auto& placement : sheetResult.placements) {
            float w = placement.getWidth() * scale;
            float h = placement.getHeight() * scale;
            float x = offsetX + placement.x * scale;
            float y = offsetY + placement.y * scale;

            ImU32 color = colors[placement.partIndex % numColors];

            area.drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), color);
            area.drawList->AddRect(ImVec2(x, y), ImVec2(x + w, y + h),
                                   IM_COL32(255, 255, 255, 128));

            // Part label
            if (placement.part && w > 30 && h > 15) {
                ImVec2 textPos(x + 3, y + 2);
                area.drawList->AddText(textPos, IM_COL32(255, 255, 255, 255),
                                       placement.part->name.c_str());
            }
        }
    }

    m_canvas.handleInput("cut_canvas");
}

// ---------------------------------------------------------------------------
// LAYOUTS SIDEBAR — per-sheet efficiency bars
// ---------------------------------------------------------------------------
void CutOptimizerPanel::renderLayoutsSidebar() {
    int sheetCount = m_hasResults ? static_cast<int>(m_result.sheets.size()) : 0;
    ImGui::Text("%s LAYOUTS  %d sheets", Icons::Optimizer, sheetCount);

    if (!m_hasResults || m_result.sheets.empty()) {
        ImGui::TextDisabled("No results");
        return;
    }

    for (int i = 0; i < static_cast<int>(m_result.sheets.size()); ++i) {
        const auto& sr = m_result.sheets[static_cast<size_t>(i)];
        float eff = sr.efficiency();
        int pieces = static_cast<int>(sr.placements.size());

        ImGui::PushID(i);

        bool selected = (m_selectedSheet == i);
        char label[64];
        std::snprintf(label, sizeof(label), "Sheet %d", i + 1);

        if (ImGui::Selectable(label, selected)) {
            m_selectedSheet = i;
        }

        // Piece count + efficiency on same line
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("00pc 100%").x);
        ImGui::Text("%dpc %d%%", pieces, static_cast<int>(eff * 100));

        // Color-coded progress bar
        ImVec4 barColor;
        if (eff > 0.75f)
            barColor = ImVec4(0.2f, 0.7f, 0.3f, 1.0f); // green
        else if (eff > 0.50f)
            barColor = ImVec4(0.85f, 0.65f, 0.1f, 1.0f); // yellow
        else
            barColor = ImVec4(0.8f, 0.2f, 0.2f, 1.0f); // red

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
        ImGui::ProgressBar(eff, ImVec2(-1, 4), "");
        ImGui::PopStyleColor();

        ImGui::PopID();
    }
}

// ---------------------------------------------------------------------------
// RESULTS PANEL — stats + cost + waste breakdown
// ---------------------------------------------------------------------------
void CutOptimizerPanel::renderResultsPanel() {
    ImGui::Text("%s RESULTS", Icons::Info);

    if (!m_hasResults) {
        ImGui::TextDisabled("Run optimizer first");
        return;
    }

    // Multi-stock results with waste breakdown
    if (m_hasMultiResults && !m_multiResult.groups.empty()) {
        // Group selector if multiple groups
        if (m_multiResult.groups.size() > 1) {
            ImGui::TextDisabled("Material Groups:");
            for (int gi = 0; gi < static_cast<int>(m_multiResult.groups.size()); ++gi) {
                const auto& g = m_multiResult.groups[static_cast<size_t>(gi)];
                char label[128];
                std::snprintf(label, sizeof(label), "%s##grp%d",
                              g.materialName.c_str(), gi);
                bool isSelected = (m_selectedGroupIdx == gi);
                if (ImGui::Selectable(label, isSelected)) {
                    m_selectedGroupIdx = gi;
                    m_result = g.plan;
                    m_sheet = g.usedSheet;
                    std::strncpy(m_sheetName, m_sheet.name.c_str(), sizeof(m_sheetName));
                    m_sheetName[sizeof(m_sheetName) - 1] = '\0';
                    m_selectedSheet = 0;
                }
            }
            ImGui::Separator();
        }

        // Current group details
        int groupIdx = std::clamp(m_selectedGroupIdx, 0,
                                   static_cast<int>(m_multiResult.groups.size()) - 1);
        const auto& grp = m_multiResult.groups[static_cast<size_t>(groupIdx)];

        float eff = grp.plan.overallEfficiency() * 100.0f;
        ImGui::Text("%.1f%%", static_cast<double>(eff));
        ImGui::TextDisabled("Efficiency - %s", grp.materialName.c_str());

        ImGui::Spacing();
        ImGui::Text("Stock: %s", grp.usedSheet.name.c_str());
        ImGui::Text("Sheets: %d", grp.plan.sheetsUsed);
        ImGui::Text("Cost: $%.2f", static_cast<double>(grp.plan.totalCost));

        // Waste breakdown
        const auto& wb = grp.waste;
        ImGui::Spacing();
        ImGui::TextDisabled("Waste Breakdown:");

        ImGui::Text("  Usable Scrap: $%.2f", static_cast<double>(wb.scrapValue));
        ImGui::TextDisabled("    %.0f sq mm", static_cast<double>(wb.totalScrapArea));
        ImGui::Text("  Kerf Loss:    $%.2f", static_cast<double>(wb.kerfValue));
        ImGui::TextDisabled("    %.0f sq mm", static_cast<double>(wb.totalKerfArea));
        ImGui::Text("  Unusable:     $%.2f", static_cast<double>(wb.unusableValue));
        ImGui::Text("  Total Waste:  $%.2f", static_cast<double>(wb.totalWasteValue));

        // Scrap pieces collapsible
        if (!wb.scrapPieces.empty() &&
            ImGui::TreeNode("Scrap Pieces")) {
            for (size_t si = 0; si < wb.scrapPieces.size(); ++si) {
                const auto& sp = wb.scrapPieces[si];
                // Convert sq mm to sq inches for user-friendliness
                float sqIn = sp.area() / 645.16f;
                ImGui::Text("  %.0f x %.0f mm (%.1f sq in) - Sheet %d",
                            static_cast<double>(sp.width),
                            static_cast<double>(sp.height),
                            static_cast<double>(sqIn),
                            sp.sheetIndex + 1);
            }
            ImGui::TreePop();
        }

        // Unplaced warning for this group
        if (!grp.plan.unplacedParts.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, colors::kErrorText);
            ImGui::TextWrapped("%zu parts could not be placed!",
                               grp.plan.unplacedParts.size());
            ImGui::PopStyleColor();
        }

        // Grand totals across all groups
        if (m_multiResult.groups.size() > 1) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Grand Total: $%.2f (%d sheets)",
                         static_cast<double>(m_multiResult.totalCost),
                         m_multiResult.totalSheetsUsed);
        }
    } else {
        // Single-sheet results (backward compatible)
        float eff = m_result.overallEfficiency() * 100.0f;

        ImGui::Text("%.1f%%", static_cast<double>(eff));
        ImGui::TextDisabled("Efficiency");

        ImGui::Spacing();
        ImGui::Text("Sheets: %d", m_result.sheetsUsed);

        // Compute waste breakdown for single-sheet results
        auto wb = optimizer::computeWasteBreakdown(m_result, m_sheet, m_kerf);

        float totalCost = static_cast<float>(m_result.sheetsUsed) * m_sheet.cost;
        ImGui::Spacing();
        ImGui::Text("Cost: $%.2f", static_cast<double>(totalCost));
        ImGui::TextDisabled("%d x $%.2f", m_result.sheetsUsed, static_cast<double>(m_sheet.cost));

        // Waste breakdown
        ImGui::Spacing();
        ImGui::TextDisabled("Waste Breakdown:");
        ImGui::Text("  Usable Scrap: $%.2f", static_cast<double>(wb.scrapValue));
        ImGui::Text("  Kerf Loss:    $%.2f", static_cast<double>(wb.kerfValue));
        ImGui::Text("  Unusable:     $%.2f", static_cast<double>(wb.unusableValue));
        ImGui::Text("  Total Waste:  $%.2f", static_cast<double>(wb.totalWasteValue));

        // Scrap pieces
        if (!wb.scrapPieces.empty() &&
            ImGui::TreeNode("Scrap Pieces")) {
            for (const auto& sp : wb.scrapPieces) {
                float sqIn = sp.area() / 645.16f;
                ImGui::Text("  %.0f x %.0f mm (%.1f sq in) - Sheet %d",
                            static_cast<double>(sp.width),
                            static_cast<double>(sp.height),
                            static_cast<double>(sqIn),
                            sp.sheetIndex + 1);
            }
            ImGui::TreePop();
        }

        // Unplaced warning
        if (!m_result.unplacedParts.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, colors::kErrorText);
            ImGui::TextWrapped("%zu parts could not be placed!",
                               m_result.unplacedParts.size());
            ImGui::PopStyleColor();
        }
    }

    // Add to Project Costing button
    ImGui::Spacing();
    if (!m_onAddToCost)
        ImGui::BeginDisabled();
    if (ImGui::Button("Add to Costing", ImVec2(-1, 0))) {
        if (m_onAddToCost) {
            std::vector<CloGroupCostData> groups;

            if (m_hasMultiResults) {
                // Multi-stock: one entry per material group
                for (const auto& gr : m_multiResult.groups) {
                    CloGroupCostData data;
                    data.materialName = gr.materialName;

                    // Format dimensions from usedSheet
                    char dimBuf[64];
                    std::snprintf(dimBuf, sizeof(dimBuf), "%.0fx%.0fmm",
                                  static_cast<double>(gr.usedSheet.width),
                                  static_cast<double>(gr.usedSheet.height));
                    data.dimensions = dimBuf;

                    data.sheetsUsed = gr.plan.sheetsUsed;
                    data.costPerSheet = static_cast<f64>(gr.usedSheet.cost);
                    data.totalCost = data.costPerSheet * data.sheetsUsed;

                    // Look up StockSize DB ID from m_stockSizes by matching dimensions
                    for (const auto& ss : m_stockSizes) {
                        if (std::abs(static_cast<f32>(ss.widthMm) - gr.usedSheet.width) < 1.0f &&
                            std::abs(static_cast<f32>(ss.heightMm) - gr.usedSheet.height) < 1.0f) {
                            data.stockSizeDbId = ss.id;
                            break;
                        }
                    }

                    groups.push_back(std::move(data));
                }
            } else if (m_hasResults) {
                // Single-sheet mode: one entry
                CloGroupCostData data;
                data.materialName = m_sheet.name.empty() ? "Cut Plan Sheets" : m_sheet.name;

                char dimBuf[64];
                std::snprintf(dimBuf, sizeof(dimBuf), "%.0fx%.0fmm",
                              static_cast<double>(m_sheet.width),
                              static_cast<double>(m_sheet.height));
                data.dimensions = dimBuf;

                data.sheetsUsed = m_result.sheetsUsed;
                data.costPerSheet = static_cast<f64>(m_sheet.cost);
                data.totalCost = data.costPerSheet * data.sheetsUsed;
                groups.push_back(std::move(data));
            }

            m_onAddToCost(groups);
            ToastManager::instance().show(ToastType::Success, "Added",
                                          "Cut plan cost added to costing");
        }
    }
    if (!m_onAddToCost)
        ImGui::EndDisabled();
}

// ---------------------------------------------------------------------------
// STATUS BAR
// ---------------------------------------------------------------------------
void CutOptimizerPanel::renderStatusBar() {
    // Count unique part types and total pieces
    int types = static_cast<int>(m_parts.size());
    int totalPieces = 0;
    for (const auto& p : m_parts)
        totalPieces += p.quantity;

    int sheetsUsed = m_hasResults ? m_result.sheetsUsed : 0;
    float eff = m_hasResults ? m_result.overallEfficiency() * 100.0f : 0.0f;
    int placed = 0;
    if (m_hasResults) {
        for (const auto& sr : m_result.sheets)
            placed += static_cast<int>(sr.placements.size());
    }

    ImGui::Separator();
    ImGui::Text("%d types  %s  %d pieces    %s    %d sheets  %s  %.1f%% eff    %s    %d placed",
                types, "\xc2\xb7", totalPieces,
                "\xc2\xb7",
                sheetsUsed, "\xc2\xb7", static_cast<double>(eff),
                "\xc2\xb7",
                placed);
}

// ---------------------------------------------------------------------------
// IMPORT FROM PROJECT popup
// ---------------------------------------------------------------------------
void CutOptimizerPanel::renderImportPopup() {
    if (m_importPopupOpen) {
        ImGui::OpenPopup("Import from Project");
        m_importPopupOpen = false;
    }

    if (ImGui::BeginPopupModal("Import from Project", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!m_modelRepo || !m_projectManager || !m_projectManager->currentProject()) {
            ImGui::Text("No project open");
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        auto& modelIds = m_projectManager->currentProject()->modelIds();
        if (m_importSelection.size() != modelIds.size())
            m_importSelection.assign(modelIds.size(), 1);

        ImGui::Text("Select models to import as cut parts:");
        ImGui::Text("Projection: XY footprint");
        ImGui::Separator();

        const auto* vp = ImGui::GetMainViewport();
        ImVec2 listSize(vp->WorkSize.x * 0.3f, vp->WorkSize.y * 0.3f);
        if (ImGui::BeginChild("ModelList", listSize, ImGuiChildFlags_Borders)) {
            for (size_t i = 0; i < modelIds.size(); ++i) {
                auto record = m_modelRepo->findById(modelIds[i]);
                if (!record)
                    continue;

                ImGui::PushID(static_cast<int>(i));
                bool sel = m_importSelection[i] != 0;
                if (ImGui::Checkbox("##sel", &sel))
                    m_importSelection[i] = sel ? 1 : 0;
                ImGui::SameLine();

                // Model name + XZ dimensions
                Vec3 size = record->boundsMax - record->boundsMin;
                float partW = size.x;
                float partH = size.y;
                ImGui::Text("%s  (%.0f x %.0f mm)",
                            record->name.c_str(),
                            static_cast<double>(partW),
                            static_cast<double>(partH));
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        if (ImGui::Button("Import Selected")) {
            int imported = 0;
            for (size_t i = 0; i < modelIds.size(); ++i) {
                if (!m_importSelection[i])
                    continue; // NOLINT
                auto record = m_modelRepo->findById(modelIds[i]);
                if (!record)
                    continue;

                Vec3 size = record->boundsMax - record->boundsMin;
                optimizer::Part part;
                part.id = record->id;
                part.name = record->name;
                part.width = size.x;
                part.height = size.y;
                part.quantity = 1;
                m_parts.push_back(part);
                ++imported;
            }
            m_hasResults = false;
            m_importSelection.clear();
            ToastManager::instance().show(ToastType::Success, "Imported",
                                          std::to_string(imported) + " parts added");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_importSelection.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// runOptimization — uses MultiStockOptimizer when materials are assigned
// ---------------------------------------------------------------------------
void CutOptimizerPanel::runOptimization() {
    if (m_parts.empty()) {
        ToastManager::instance().show(ToastType::Warning, "No Parts",
                                      "Add parts before running optimization");
        return;
    }

    // Check if any parts have material assignments
    bool hasMaterialAssignments = false;
    for (const auto& part : m_parts) {
        if (part.materialId != 0) {
            hasMaterialAssignments = true;
            break;
        }
    }

    if (hasMaterialAssignments && m_materialManager) {
        // Build material groups from parts
        std::map<i64, optimizer::MaterialGroup> groupMap;
        for (const auto& part : m_parts) {
            i64 matId = part.materialId;
            if (groupMap.find(matId) == groupMap.end()) {
                optimizer::MaterialGroup mg;
                mg.materialId = matId;
                if (matId == 0) {
                    mg.materialName = "Unassigned";
                    mg.stockSizes = {m_sheet};
                } else {
                    auto mat = m_materialManager->getMaterial(matId);
                    mg.materialName = mat ? mat->name : "Unknown";
                    auto stockSizes = m_materialManager->getStockSizes(matId);
                    for (const auto& ss : stockSizes) {
                        optimizer::Sheet s;
                        s.width = static_cast<f32>(ss.widthMm);
                        s.height = static_cast<f32>(ss.heightMm);
                        s.cost = static_cast<f32>(ss.pricePerUnit);
                        s.name = ss.name;
                        mg.stockSizes.push_back(s);
                    }
                    // If no stock sizes defined, use current sheet as fallback
                    if (mg.stockSizes.empty()) {
                        mg.stockSizes = {m_sheet};
                    }
                }
                groupMap[matId] = mg;
            }
        }

        std::vector<optimizer::MaterialGroup> materials;
        materials.reserve(groupMap.size());
        for (auto& [id, mg] : groupMap) {
            materials.push_back(std::move(mg));
        }

        m_multiResult = optimizer::optimizeMultiStock(
            m_parts, materials, m_algorithm, m_allowRotation, m_kerf, m_margin);
        m_hasMultiResults = true;
        m_selectedGroupIdx = 0;

        // Populate backward-compatible single result from first group
        if (!m_multiResult.groups.empty()) {
            m_result = m_multiResult.groups[0].plan;
            m_sheet = m_multiResult.groups[0].usedSheet;
            std::strncpy(m_sheetName, m_sheet.name.c_str(), sizeof(m_sheetName));
            m_sheetName[sizeof(m_sheetName) - 1] = '\0';
        }
        m_hasResults = true;
        m_selectedSheet = 0;

        // Check for unplaced across all groups
        int totalUnplaced = 0;
        for (const auto& g : m_multiResult.groups)
            totalUnplaced += static_cast<int>(g.plan.unplacedParts.size());

        if (totalUnplaced > 0) {
            ToastManager::instance().show(ToastType::Warning, "Incomplete Layout",
                                          std::to_string(totalUnplaced) + " parts could not be placed");
        } else {
            ToastManager::instance().show(ToastType::Success, "Optimization Complete",
                                          std::to_string(m_multiResult.groups.size()) +
                                              " material groups optimized");
        }
    } else {
        // Single-sheet optimization (no material assignments)
        auto opt = optimizer::CutOptimizer::create(m_algorithm);
        if (!opt) {
            ToastManager::instance().show(ToastType::Error, "Optimizer Error",
                                          "Failed to create optimizer");
            return;
        }

        opt->setAllowRotation(m_allowRotation);
        opt->setKerf(m_kerf);
        opt->setMargin(m_margin);

        std::vector<optimizer::Sheet> sheets = {m_sheet};
        m_result = opt->optimize(m_parts, sheets);
        m_hasResults = true;
        m_hasMultiResults = false;
        m_selectedSheet = 0;

        int unplacedCount = static_cast<int>(m_result.unplacedParts.size());
        if (unplacedCount > 0) {
            ToastManager::instance().show(ToastType::Warning, "Incomplete Layout",
                                          std::to_string(unplacedCount) + " parts could not be placed");
        } else {
            ToastManager::instance().show(ToastType::Success, "Optimization Complete",
                                          "All parts placed successfully");
        }
    }
}

} // namespace dw

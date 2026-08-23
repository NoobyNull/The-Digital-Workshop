#include "supplier_tool_import_dialog.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

#include <imgui.h>

#include "core/database/tool_database.h"
#include "core/database/toolbox_repository.h"
#include "ui/widgets/toast.h"

namespace dw {
namespace {

const char* toolTypeLabel(VtdbToolType type) {
    switch (type) {
    case VtdbToolType::BallNose: return "Ball Nose";
    case VtdbToolType::EndMill: return "End Mill";
    case VtdbToolType::Radiused: return "Radiused";
    case VtdbToolType::VBit: return "V-Bit";
    case VtdbToolType::TaperedBallNose: return "Tapered Ball Nose";
    case VtdbToolType::Drill: return "Drill";
    case VtdbToolType::ThreadMill: return "Thread Mill";
    case VtdbToolType::FormTool: return "Form Tool";
    case VtdbToolType::DiamondDrag: return "Diamond Drag";
    }
    return "Unknown";
}

std::string categoryLabel(const std::vector<std::string>& path) {
    std::ostringstream label;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i > 0)
            label << " / ";
        label << path[i];
    }
    return label.str();
}

std::string sizeLabel(const SupplierToolSummary& tool) {
    char label[64];
    std::snprintf(label, sizeof(label), "%.4g %s",
                  static_cast<double>(tool.diameter),
                  tool.units == VtdbUnits::Metric ? "mm" : "in");
    return label;
}

std::string copiedMessage(const SelectiveToolImportResult& result,
                          std::size_t toolboxReady,
                          std::size_t toolboxFailed,
                          bool requestedToolbox) {
    std::ostringstream message;
    message << "Copied " << result.copiedCount << " new tool";
    if (result.copiedCount != 1)
        message << 's';
    message << " to My Tool Library.";
    if (result.alreadyPresentCount > 0)
        message << ' ' << result.alreadyPresentCount << " already present.";
    if (requestedToolbox)
        message << ' ' << toolboxReady << " available in My Toolbox.";
    if (toolboxFailed > 0)
        message << ' ' << toolboxFailed << " could not be added to My Toolbox.";
    return message.str();
}

} // namespace

SupplierToolImportDialog::SupplierToolImportDialog()
    : Dialog("Copy Tools from Supplier Database") {}

void SupplierToolImportDialog::openSource(const Path& sourcePath) {
    m_catalog.close();
    m_sourcePath = sourcePath;
    m_rows.clear();
    m_tree = SupplierToolImportTree{};
    m_activeFolderId.clear();
    m_selection.clear();
    m_filter[0] = '\0';
    m_openError.clear();
    m_addToToolbox = true;

    if (!m_toolDatabase) {
        m_openError = "My Tool Library is not available.";
    } else {
        const auto opened = m_catalog.open(sourcePath);
        if (!opened) {
            m_openError = opened.message.empty()
                ? "This is not a populated Vectric tool database."
                : opened.message;
        } else {
            rebuildRows();
            if (m_rows.empty())
                m_openError = "This database does not contain any tools.";
        }
    }
    m_open = true;
}

void SupplierToolImportDialog::rebuildRows() {
    m_rows.clear();
    m_rows.reserve(m_catalog.tools().size());
    for (const auto& tool : m_catalog.tools()) {
        SupplierToolImportRow row;
        row.geometryId = tool.geometryId;
        row.displayName = tool.displayName;
        row.categoryPath = tool.categoryPath;
        row.folderPath = categoryLabel(tool.categoryPath);
        row.toolType = toolTypeLabel(tool.toolType);
        row.size = sizeLabel(tool);
        row.flutes = tool.numFlutes;
        row.alreadyLocal = m_toolDatabase->findGeometryById(tool.geometryId).has_value();
        row.inToolbox = m_toolboxRepository && m_toolboxRepository->hasTool(tool.geometryId);
        m_rows.push_back(std::move(row));
    }
    m_tree = SupplierToolImportTree(m_rows);
    m_activeFolderId = m_tree.rootId();
}

void SupplierToolImportDialog::closeDialog() {
    m_selection.clear();
    m_catalog.close();
    m_tree = SupplierToolImportTree{};
    m_activeFolderId.clear();
    m_open = false;
    ImGui::CloseCurrentPopup();
}

const char* SupplierToolImportDialog::toolStatusLabel(const SupplierToolImportRow& row) {
    if (row.alreadyLocal && row.inToolbox)
        return "In library / My Toolbox";
    if (row.alreadyLocal)
        return "In library";
    return "Ready to copy";
}

void SupplierToolImportDialog::copySelected() {
    if (!m_toolDatabase || !m_catalog.isOpen() || m_selection.count() == 0)
        return;

    auto result = SelectiveToolImporter::copySelected(
        m_catalog, *m_toolDatabase, m_selection.geometryIds());
    if (!result) {
        ToastManager::instance().show(
            ToastType::Error, "Tools were not copied", result.message, 6.0f);
        return;
    }

    std::size_t toolboxReady = 0;
    std::size_t toolboxFailed = 0;
    if (m_addToToolbox && m_toolboxRepository) {
        for (const auto& item : result.items) {
            if (m_toolboxRepository->hasTool(item.localGeometryId) ||
                m_toolboxRepository->addTool(item.localGeometryId)) {
                ++toolboxReady;
            } else {
                ++toolboxFailed;
            }
        }
    }

    const ToastType toastType = toolboxFailed > 0 ? ToastType::Warning : ToastType::Success;
    ToastManager::instance().show(
        toastType,
        toolboxFailed > 0 ? "Tools copied with a warning" : "Tools copied",
        copiedMessage(result, toolboxReady, toolboxFailed,
                      m_addToToolbox && m_toolboxRepository),
        6.0f);
    if (m_onImported)
        m_onImported(result);
    closeDialog();
}

void SupplierToolImportDialog::render() {
    if (!m_open)
        return;

    ImGui::OpenPopup(m_title.c_str());
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(
        ImVec2(viewport->WorkSize.x * 0.78f, viewport->WorkSize.y * 0.72f),
        ImGuiCond_Appearing);

    bool keepOpen = m_open;
    if (!ImGui::BeginPopupModal(
            m_title.c_str(), &keepOpen,
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse)) {
        m_open = keepOpen;
        return;
    }
    if (!keepOpen) {
        closeDialog();
        ImGui::EndPopup();
        return;
    }

    ImGui::Text("Source: %s", m_sourcePath.filename().string().c_str());
    if (!m_openError.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_openError.c_str());
        ImGui::TextDisabled("Choose another .vtdb file from Import Tools.");
        ImGui::Spacing();
        if (ImGui::Button("Close"))
            closeDialog();
        ImGui::EndPopup();
        return;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%zu tools", m_rows.size());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##supplierToolSearch",
                             "Search name, part number, folder, type, or size...",
                             m_filter, sizeof(m_filter));

    const std::string query(m_filter);
    const auto visibleRows = scopedRows(query);
    const auto* scope = activeFolder();
    const std::size_t scopeCount = scope ? scope->totalToolCount : m_rows.size();

    if (ImGui::Button("Select all shown")) {
        m_tree.setBranchSelected(m_activeFolderId, m_rows, query, true, m_selection);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear selection"))
        m_selection.clear();
    ImGui::SameLine();
    ImGui::TextDisabled("Showing %zu of %zu in %s  |  %zu selected",
                        visibleRows.size(), scopeCount,
                        scope ? scope->label.c_str() : "All Tools", m_selection.count());

    const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 3.0f;
    const float browserHeight =
        std::max(120.0f, ImGui::GetContentRegionAvail().y - footerHeight);
    renderBrowser(query, browserHeight);

    if (!m_toolboxRepository)
        ImGui::BeginDisabled();
    ImGui::Checkbox("Also add copied tools to My Toolbox (installed / owned)",
                    &m_addToToolbox);
    if (!m_toolboxRepository)
        ImGui::EndDisabled();
    ImGui::TextDisabled("My Tool Library stores the catalog; My Toolbox is your ready-to-use shortlist.");

    const std::string actionLabel = supplierToolImportActionLabel(m_selection.count());
    if (m_selection.count() == 0)
        ImGui::BeginDisabled();
    if (ImGui::Button(actionLabel.c_str()))
        copySelected();
    if (m_selection.count() == 0)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        closeDialog();

    ImGui::EndPopup();
}

} // namespace dw

#include "library_panel.h"

#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>

#include "../../core/config/config.h"
#include "../../core/paths/path_resolver.h"
#include "../../core/utils/file_utils.h"
#include "../context_menu_manager.h"

namespace dw {

void LibraryPanel::registerContextMenuEntries() {
    if (!m_contextMenuManager)
        return;

    const auto modelCount = m_selectedModelIds.size();
    const bool multipleModels = modelCount > 1;
    const std::string modelCountSuffix =
        multipleModels ? " (" + std::to_string(modelCount) + ")" : "";
    std::vector<ContextMenuEntry> modelEntries = {
        {"Preview",
         [this]() {
             if (m_currentContextMenuModel) {
                 emitPreviewRequested({workshop::LibraryItemKind::Model,
                                       workshop::LibraryItemId(
                                           m_currentContextMenuModel->id)});
             }
         }},
        {"Regenerate Thumbnail" +
             (multipleModels ? "s" + modelCountSuffix : ""),
         [this]() {
             if (!m_onRegenerateThumbnail)
                 return;
             m_onRegenerateThumbnail(
                 {m_selectedModelIds.begin(), m_selectedModelIds.end()});
         }},
        {"Assign Library Default Material" + modelCountSuffix,
         [this]() {
             if (!m_onAssignDefaultMaterial)
                 return;
             for (const auto id : m_selectedModelIds)
                 m_onAssignDefaultMaterial(id);
         },
         "",
         []() { return Config::instance().getDefaultMaterialId() > 0; }},
        {"Assign Category" + modelCountSuffix,
         [this]() { m_showCategoryAssignDialog = true; }},
        {"AI Retag" + modelCountSuffix,
         [this]() {
             if (m_onTagImage && !m_selectedModelIds.empty()) {
                 m_onTagImage(
                     {m_selectedModelIds.begin(), m_selectedModelIds.end()});
             }
         }},
        ContextMenuEntry::separator(),
        {"Rename",
         [this]() {
             if (!m_currentContextMenuModel)
                 return;
             m_showRenameDialog = true;
             m_renameModelId = m_currentContextMenuModel->id;
             std::strncpy(m_renameBuffer,
                          m_currentContextMenuModel->name.c_str(),
                          sizeof(m_renameBuffer) - 1);
             m_renameBuffer[sizeof(m_renameBuffer) - 1] = '\0';
         },
         "",
         [this]() { return m_selectedModelIds.size() == 1; }},
        {"Delete" + modelCountSuffix,
         [this]() {
             m_showDeleteConfirm = true;
             m_deleteItems = selectedLibraryItems();
             m_deleteResultMessage.clear();
             m_deleteItemName = m_deleteItems.size() == 1 && m_currentContextMenuModel
                                    ? m_currentContextMenuModel->name
                                    : std::to_string(m_deleteItems.size()) + " items";
         }},
        ContextMenuEntry::separator(),
        {"Show in Explorer",
         [this]() {
             if (!m_currentContextMenuModel)
                 return;
             const auto parent = PathResolver::fileManagerParent(
                 m_currentContextMenuModel->filePath, PathCategory::Support);
             if (!parent.empty())
                 file::openInFileManager(parent);
         }},
        {"Copy Path",
         [this]() {
             if (m_selectedModelIds.size() > 1) {
                 std::string paths;
                 for (const auto id : m_selectedModelIds) {
                     for (const auto& model : m_models) {
                         if (model.id != id)
                             continue;
                         if (!paths.empty())
                             paths += "\n";
                         paths += PathResolver::durableLocation(
                                      model.filePath, PathCategory::Support).string();
                         break;
                     }
                 }
                 ImGui::SetClipboardText(paths.c_str());
             } else if (m_currentContextMenuModel) {
                 const auto path = PathResolver::durableLocation(
                     m_currentContextMenuModel->filePath, PathCategory::Support);
                 ImGui::SetClipboardText(path.string().c_str());
             }
         }},
    };

    const auto gcodeCount = m_selectedGCodeIds.size();
    const bool multipleGCodes = gcodeCount > 1;
    const std::string gcodeCountSuffix =
        multipleGCodes ? " (" + std::to_string(gcodeCount) + ")" : "";
    std::vector<ContextMenuEntry> gcodeEntries = {
        {"Preview",
         [this]() {
             if (m_currentContextMenuGCode) {
                 emitPreviewRequested({workshop::LibraryItemKind::GCode,
                                       workshop::LibraryItemId(
                                           m_currentContextMenuGCode->id)});
             }
         }},
        ContextMenuEntry::separator(),
        {"Delete" + gcodeCountSuffix,
         [this]() {
             m_showDeleteConfirm = true;
             m_deleteItems = selectedLibraryItems();
             m_deleteResultMessage.clear();
             m_deleteItemName = m_deleteItems.size() == 1 && m_currentContextMenuGCode
                                    ? m_currentContextMenuGCode->name
                                    : std::to_string(m_deleteItems.size()) + " items";
         }},
        ContextMenuEntry::separator(),
        {"Show in Explorer",
         [this]() {
             if (!m_currentContextMenuGCode)
                 return;
             const auto parent = PathResolver::fileManagerParent(
                 m_currentContextMenuGCode->filePath, PathCategory::GCode);
             if (!parent.empty())
                 file::openInFileManager(parent);
         }},
        {"Copy Path",
         [this]() {
             if (m_selectedGCodeIds.size() > 1) {
                 std::string paths;
                 for (const auto id : m_selectedGCodeIds) {
                     for (const auto& gcode : m_gcodeFiles) {
                         if (gcode.id != id)
                             continue;
                         if (!paths.empty())
                             paths += "\n";
                         paths += PathResolver::durableLocation(
                                      gcode.filePath, PathCategory::GCode).string();
                         break;
                     }
                 }
                 ImGui::SetClipboardText(paths.c_str());
             } else if (m_currentContextMenuGCode) {
                 const auto path = PathResolver::durableLocation(
                     m_currentContextMenuGCode->filePath, PathCategory::GCode);
                 ImGui::SetClipboardText(path.string().c_str());
             }
         }},
    };

    m_contextMenuManager->registerEntries("LibraryPanel_ModelContext", modelEntries);
    m_contextMenuManager->registerEntries("LibraryPanel_GCodeContext", gcodeEntries);
}

} // namespace dw

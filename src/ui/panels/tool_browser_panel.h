#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "../../core/cnc/cnc_tool.h"
#include "../../core/cnc/tool_calculator.h"
#include "../../core/gcode/machine_profile.h"
#include "panel.h"

namespace dw {

class ToolDatabase;
class ToolboxRepository;
class MaterialManager;
class FileDialog;
class SupplierToolImportDialog;

inline bool toolBrowserSelectionNeedsReload(const std::string& loadedGeometryId,
                                            const std::string& selectedGeometryId) {
    return !selectedGeometryId.empty() && loadedGeometryId != selectedGeometryId;
}

inline bool toolBrowserMachineProfileConfigured(const gcode::MachineProfile& profile) {
    return !profile.name.empty()
        && profile.name != "Default"
        && profile.maxTravelX > 0.0f
        && profile.maxTravelY > 0.0f
        && profile.maxTravelZ > 0.0f
        && profile.spindleMaxRPM > 0.0f
        && profile.spindlePower > 0.0f;
}

inline std::string toolBrowserMachineProfileLabel(const gcode::MachineProfile& profile) {
    return toolBrowserMachineProfileConfigured(profile)
        ? profile.name
        : std::string("Machine not configured");
}

class ToolBrowserPanel : public Panel {
  public:
    ToolBrowserPanel();
    ~ToolBrowserPanel() override = default;

    void render() override;

    void setToolDatabase(ToolDatabase* db) { m_toolDatabase = db; }
    void setToolboxRepository(ToolboxRepository* repo) { m_toolboxRepo = repo; }
    void setMaterialManager(MaterialManager* mgr) { m_materialManager = mgr; }
    void setFileDialog(FileDialog* dlg) { m_fileDialog = dlg; }
    void setSupplierToolImportDialog(SupplierToolImportDialog* dialog) {
        m_supplierToolImportDialog = dialog;
    }
    void setOpenMachineProfilesCallback(std::function<void()> cb) {
        m_openMachineProfiles = std::move(cb);
    }
    void onMachineProfileChanged();
    void refresh();

  private:
    void renderToolbar();
    void renderTree();
    void renderToolDetail();
    void renderCalculator();
    void renderMachineEditor();
    void renderAddToolPopup();
    void renderAddGroupPopup();

    void loadData();
    void selectTool(const std::string& geometryId);
    void deleteSelected();
    void runCalculation();

    ToolDatabase* m_toolDatabase = nullptr;
    ToolboxRepository* m_toolboxRepo = nullptr;
    MaterialManager* m_materialManager = nullptr;
    FileDialog* m_fileDialog = nullptr;
    SupplierToolImportDialog* m_supplierToolImportDialog = nullptr;

    // Cached data
    std::vector<VtdbTreeEntry> m_treeEntries;
    std::vector<VtdbMaterial> m_materials;
    std::vector<VtdbToolGeometry> m_geometries;

    // Selection state
    std::string m_selectedTreeEntryId;
    std::string m_selectedGeometryId;
    std::string m_loadedGeometryId;
    std::string m_selectedMaterialId;

    // Editing state
    VtdbToolGeometry m_editGeometry;
    VtdbCuttingData m_editCuttingData;
    bool m_hasCuttingData = false;

    // Calculator state
    CalcResult m_calcResult;
    bool m_hasCalcResult = false;
    float m_calcJanka = 0.0f;
    std::string m_calcMaterialName;
    std::function<void()> m_openMachineProfiles;

    // Add Tool popup state
    bool m_showAddTool = false;
    int m_addToolType = 1; // EndMill default
    std::string m_addToolName;
    float m_addToolDiameter = 0.25f;
    int m_addToolFlutes = 2;
    std::string m_addToolParentGroupId;

    // Add Group popup state
    bool m_showAddGroup = false;
    std::string m_addGroupName;
    std::string m_addGroupParentId;

    // Toolbox state
    std::set<std::string> m_toolboxIds;

    // Search/filter state
    char m_filterBuf[128] = {};
    std::set<std::string> m_filterMatchIds;  // Entry IDs that match filter
    void updateFilter();

    // Context menu state
    std::string m_contextMenuEntryId;
    std::string m_contextMenuGeomId;
    bool m_showRenamePopup = false;
    char m_renameBuf[128] = {};
    bool m_showDeleteConfirm = false;

    void duplicateTool(const std::string& treeEntryId);

    bool m_needsRefresh = true;
};

} // namespace dw

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/database/selective_tool_importer.h"
#include "core/database/supplier_tool_catalog.h"
#include "ui/dialogs/dialog.h"
#include "ui/dialogs/supplier_tool_import_presentation.h"

namespace dw {

class ToolDatabase;
class ToolboxRepository;

class SupplierToolImportDialog : public Dialog {
  public:
    SupplierToolImportDialog();
    ~SupplierToolImportDialog() override = default;

    void render() override;
    void openSource(const Path& sourcePath);

    void setToolDatabase(ToolDatabase* database) { m_toolDatabase = database; }
    void setToolboxRepository(ToolboxRepository* repository) {
        m_toolboxRepository = repository;
    }
    void setOnImported(std::function<void(const SelectiveToolImportResult&)> callback) {
        m_onImported = std::move(callback);
    }

  private:
    void rebuildRows();
    void copySelected();
    void closeDialog();
    void renderBrowser(const std::string& query, float height);
    void renderFolderPane(const std::string& query, float width, float height);
    void renderFolderNode(const std::string& folderId, const std::string& query);
    void renderToolPane(const std::string& query, float width, float height);
    [[nodiscard]] std::vector<const SupplierToolImportRow*> scopedRows(
        const std::string& query) const;
    [[nodiscard]] const SupplierToolImportFolder* activeFolder() const;
    [[nodiscard]] static const char* toolStatusLabel(const SupplierToolImportRow& row);

    ToolDatabase* m_toolDatabase = nullptr;
    ToolboxRepository* m_toolboxRepository = nullptr;
    SupplierToolCatalog m_catalog;
    std::vector<SupplierToolImportRow> m_rows;
    SupplierToolImportTree m_tree;
    SupplierToolImportSelection m_selection;
    std::function<void(const SelectiveToolImportResult&)> m_onImported;

    Path m_sourcePath;
    std::string m_activeFolderId;
    std::string m_openError;
    char m_filter[160] = {};
    bool m_addToToolbox = true;
};

} // namespace dw

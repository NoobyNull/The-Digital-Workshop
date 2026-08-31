#include "tool_database.h"

#include "selective_tool_importer.h"
#include "supplier_tool_catalog.h"
#include "../utils/log.h"

namespace dw {

int ToolDatabase::importFromVtdb(const Path& externalPath) {
    SupplierToolCatalog catalog;
    const auto opened = catalog.open(externalPath);
    if (!opened) {
        log::errorf("ToolDatabase", "Cannot open supplier vtdb: %s (%s)",
                    externalPath.string().c_str(), opened.message.c_str());
        return -1;
    }

    std::vector<std::string> geometryIds;
    geometryIds.reserve(catalog.tools().size());
    for (const auto& tool : catalog.tools())
        geometryIds.push_back(tool.geometryId);
    if (geometryIds.empty())
        return 0;

    const auto imported = SelectiveToolImporter::copySelected(catalog, *this, geometryIds);
    if (!imported) {
        log::errorf("ToolDatabase", "Failed to import supplier vtdb: %s",
                    imported.message.c_str());
        return -1;
    }

    log::infof("ToolDatabase", "Imported %zu geometries from %s (%zu already present)",
               imported.copiedCount, externalPath.string().c_str(),
               imported.alreadyPresentCount);
    return static_cast<int>(imported.copiedCount);
}

} // namespace dw

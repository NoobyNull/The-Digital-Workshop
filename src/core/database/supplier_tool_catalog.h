#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../cnc/cnc_tool.h"
#include "../types.h"

namespace dw {

class SelectiveToolImporter;

enum class SupplierToolCatalogError {
    None,
    CannotOpen,
    InvalidSchema,
    UnsupportedSchema,
    QueryFailed,
};

struct SupplierToolCatalogOpenResult {
    SupplierToolCatalogError error = SupplierToolCatalogError::None;
    std::string message;

    explicit operator bool() const { return error == SupplierToolCatalogError::None; }
};

struct SupplierToolSummary {
    std::string geometryId;
    std::string treeEntryId;
    std::string displayName;
    std::vector<std::string> categoryPath;
    VtdbToolType toolType = VtdbToolType::EndMill;
    VtdbUnits units = VtdbUnits::Imperial;
    f64 diameter = 0.0;
    int numFlutes = 0;
    usize cuttingProfileCount = 0;
};

// Read-only view of a supplier's Vectric tool database. The catalog owns its
// source connection and never exposes writable database operations to the UI.
class SupplierToolCatalog {
  public:
    SupplierToolCatalog();
    ~SupplierToolCatalog();
    SupplierToolCatalog(SupplierToolCatalog&&) noexcept;
    SupplierToolCatalog& operator=(SupplierToolCatalog&&) noexcept;

    SupplierToolCatalog(const SupplierToolCatalog&) = delete;
    SupplierToolCatalog& operator=(const SupplierToolCatalog&) = delete;

    [[nodiscard]] SupplierToolCatalogOpenResult open(const Path& path);
    void close();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] const Path& path() const;
    [[nodiscard]] const std::vector<SupplierToolSummary>& tools() const;

  private:
    struct ToolBundle {
        VtdbToolGeometry geometry;
        VtdbTreeEntry leaf;
        std::vector<VtdbTreeEntry> ancestors;
        std::vector<VtdbToolEntity> entities;
        std::vector<VtdbCuttingData> cuttingData;
        std::vector<VtdbMaterial> materials;
        std::vector<VtdbMachine> machines;
    };

    [[nodiscard]] const ToolBundle* findBundle(const std::string& geometryId) const;

    class Impl;
    std::unique_ptr<Impl> m_impl;

    friend class SelectiveToolImporter;
};

} // namespace dw

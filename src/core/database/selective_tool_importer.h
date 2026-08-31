#pragma once

#include <string>
#include <vector>

#include "../types.h"

namespace dw {

class SupplierToolCatalog;
class ToolDatabase;

enum class ToolCopyDisposition {
    Copied,
    AlreadyPresent,
};

enum class SelectiveToolImportError {
    None,
    CatalogNotOpen,
    EmptySelection,
    UnknownTool,
    BrokenSourceGraph,
    IdentityConflict,
    DestinationWriteFailed,
    CommitFailed,
};

struct ToolCopyItemResult {
    std::string sourceGeometryId;
    std::string localGeometryId;
    ToolCopyDisposition disposition = ToolCopyDisposition::Copied;
};

struct SelectiveToolImportResult {
    SelectiveToolImportError error = SelectiveToolImportError::None;
    std::string message;
    std::vector<ToolCopyItemResult> items;
    usize copiedCount = 0;
    usize alreadyPresentCount = 0;

    explicit operator bool() const { return error == SelectiveToolImportError::None; }
};

// Copies complete selected tool graphs into the local database in one
// transaction. Supplier UUIDs remain the durable identity; existing tools are
// never overwritten.
class SelectiveToolImporter {
  public:
    [[nodiscard]] static SelectiveToolImportResult copySelected(
        const SupplierToolCatalog& source,
        ToolDatabase& destination,
        const std::vector<std::string>& geometryIds);
};

} // namespace dw

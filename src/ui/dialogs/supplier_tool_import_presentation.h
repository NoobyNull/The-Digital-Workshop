#pragma once

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace dw {

// Render-independent row used by the supplier-tool picker. Keeping filtering
// and selection outside ImGui makes the behavior deterministic and testable.
struct SupplierToolImportRow {
    std::string geometryId;
    std::string displayName;
    std::vector<std::string> categoryPath;
    std::string folderPath;
    std::string toolType;
    std::string size;
    int flutes = 0;
    bool alreadyLocal = false;
    bool inToolbox = false;
};

bool supplierToolImportRowMatches(const SupplierToolImportRow& row, const std::string& query);

std::string supplierToolImportActionLabel(std::size_t selectedCount);

class SupplierToolImportSelection {
  public:
    [[nodiscard]] bool contains(const std::string& geometryId) const;
    void setSelected(const std::string& geometryId, bool selected);
    void selectVisible(const std::vector<SupplierToolImportRow>& rows, const std::string& query);
    void clear();

    [[nodiscard]] std::size_t count() const { return m_geometryIds.size(); }
    [[nodiscard]] std::vector<std::string> geometryIds() const;

  private:
    std::set<std::string> m_geometryIds;
};

enum class SupplierToolImportFolderKind {
    AllTools,
    Supplier,
    GeneratedType,
    GeneratedSize,
    Uncategorized,
};

enum class SupplierToolImportSelectionState {
    Unchecked,
    Mixed,
    Checked,
};

struct SupplierToolImportFolder {
    std::string id;
    std::string parentId;
    std::string label;
    SupplierToolImportFolderKind kind = SupplierToolImportFolderKind::Supplier;
    std::size_t directToolCount = 0;
    std::size_t totalToolCount = 0;
};

// Render-independent folder tree for the supplier picker. Folder IDs encode
// their namespace and full path, so they remain stable across row ordering and
// cannot collide when supplier labels contain separators.
class SupplierToolImportTree {
  public:
    explicit SupplierToolImportTree(const std::vector<SupplierToolImportRow>& rows = {});

    [[nodiscard]] const std::string& rootId() const { return m_rootId; }
    [[nodiscard]] const SupplierToolImportFolder* folder(const std::string& folderId) const;
    [[nodiscard]] std::vector<const SupplierToolImportFolder*> children(
        const std::string& folderId) const;
    [[nodiscard]] std::vector<std::string> toolIds(const std::string& folderId) const;
    [[nodiscard]] std::vector<std::string> matchingToolIds(
        const std::string& folderId,
        const std::vector<SupplierToolImportRow>& rows,
        const std::string& query,
        bool selectableOnly = false) const;

    [[nodiscard]] SupplierToolImportSelectionState selectionState(
        const std::string& folderId,
        const std::vector<SupplierToolImportRow>& rows,
        const SupplierToolImportSelection& selection,
        const std::string& query = {}) const;
    void setBranchSelected(const std::string& folderId,
                           const std::vector<SupplierToolImportRow>& rows,
                           const std::string& query,
                           bool selected,
                           SupplierToolImportSelection& selection) const;

  private:
    struct FolderNode {
        SupplierToolImportFolder folder;
        std::set<std::string> directToolIds;
        std::set<std::string> childIds;
    };

    std::vector<std::string> collectToolIds(const std::string& folderId) const;
    std::size_t updateCounts(const std::string& folderId);

    std::string m_rootId = "all";
    std::map<std::string, FolderNode> m_nodes;
};

} // namespace dw

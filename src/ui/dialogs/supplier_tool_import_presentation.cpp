#include "supplier_tool_import_presentation.h"

#include <algorithm>
#include <cctype>
#include <tuple>

namespace dw {
namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
                          return std::isspace(ch) != 0;
                      }).base();
    if (first >= last)
        return {};
    return {first, last};
}

std::string hexEncode(const std::string& value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(value.size() * 2);
    for (const char valueCharacter : value) {
        const auto ch = static_cast<unsigned char>(valueCharacter);
        encoded.push_back(digits[ch >> 4]);
        encoded.push_back(digits[ch & 0x0f]);
    }
    return encoded;
}

bool isUsefulLabel(const std::string& label) {
    const std::string normalized = lowercase(trim(label));
    return !normalized.empty() && normalized != "unknown" && normalized != "uncategorized" &&
           normalized != "n/a";
}

std::vector<std::string> supplierPath(const SupplierToolImportRow& row) {
    std::vector<std::string> result;
    for (const auto& component : row.categoryPath) {
        auto label = trim(component);
        if (!label.empty())
            result.push_back(std::move(label));
    }
    // Compatibility for callers that only populated the original flat label.
    if (result.empty()) {
        auto label = trim(row.folderPath);
        if (!label.empty())
            result.push_back(std::move(label));
    }
    return result;
}

std::string childId(const std::string& parentId,
                    const std::string& nameSpace,
                    const std::string& label) {
    return parentId + "/" + nameSpace + ":" + hexEncode(label);
}

bool folderLess(const SupplierToolImportFolder* left, const SupplierToolImportFolder* right) {
    return std::make_tuple(lowercase(left->label), left->label, left->id) <
           std::make_tuple(lowercase(right->label), right->label, right->id);
}

} // namespace

bool supplierToolImportRowMatches(const SupplierToolImportRow& row, const std::string& query) {
    if (query.empty())
        return true;

    const std::string needle = lowercase(query);
    std::string searchable = row.displayName + " " + row.folderPath + " " + row.toolType + " " +
                             row.size + " " + std::to_string(row.flutes);
    for (const auto& component : row.categoryPath)
        searchable += " " + component;
    searchable = lowercase(std::move(searchable));
    return searchable.find(needle) != std::string::npos;
}

std::string supplierToolImportActionLabel(std::size_t selectedCount) {
    if (selectedCount == 1)
        return "Copy 1 Tool";
    return "Copy " + std::to_string(selectedCount) + " Tools";
}

bool SupplierToolImportSelection::contains(const std::string& geometryId) const {
    return m_geometryIds.count(geometryId) > 0;
}

void SupplierToolImportSelection::setSelected(const std::string& geometryId, bool selected) {
    if (geometryId.empty())
        return;
    if (selected)
        m_geometryIds.insert(geometryId);
    else
        m_geometryIds.erase(geometryId);
}

void SupplierToolImportSelection::selectVisible(const std::vector<SupplierToolImportRow>& rows,
                                                const std::string& query) {
    for (const auto& row : rows) {
        if (!row.alreadyLocal && supplierToolImportRowMatches(row, query))
            m_geometryIds.insert(row.geometryId);
    }
}

void SupplierToolImportSelection::clear() {
    m_geometryIds.clear();
}

std::vector<std::string> SupplierToolImportSelection::geometryIds() const {
    return {m_geometryIds.begin(), m_geometryIds.end()};
}

SupplierToolImportTree::SupplierToolImportTree(const std::vector<SupplierToolImportRow>& rows) {
    auto& root = m_nodes[m_rootId];
    root.folder.id = m_rootId;
    root.folder.label = "All Tools";
    root.folder.kind = SupplierToolImportFolderKind::AllTools;

    for (const auto& row : rows) {
        if (row.geometryId.empty())
            continue;

        const auto path = supplierPath(row);
        std::string parentId = m_rootId;
        if (!path.empty()) {
            for (const auto& label : path) {
                const std::string id = childId(parentId, "supplier", label);
                auto& node = m_nodes[id];
                node.folder = {id, parentId, label, SupplierToolImportFolderKind::Supplier, 0, 0};
                m_nodes[parentId].childIds.insert(id);
                parentId = id;
            }
        } else if (isUsefulLabel(row.toolType)) {
            const std::string typeLabel = trim(row.toolType);
            const std::string typeId = childId(m_rootId, "generated-type", typeLabel);
            auto& typeNode = m_nodes[typeId];
            typeNode.folder = {
                typeId, m_rootId, typeLabel, SupplierToolImportFolderKind::GeneratedType, 0, 0};
            root.childIds.insert(typeId);

            const bool hasSize = isUsefulLabel(row.size);
            const std::string sizeLabel = hasSize ? trim(row.size) : "Uncategorized";
            const std::string sizeId =
                childId(typeId, hasSize ? "generated-size" : "uncategorized", sizeLabel);
            auto& sizeNode = m_nodes[sizeId];
            sizeNode.folder = {sizeId,
                               typeId,
                               sizeLabel,
                               hasSize ? SupplierToolImportFolderKind::GeneratedSize
                                       : SupplierToolImportFolderKind::Uncategorized,
                               0,
                               0};
            typeNode.childIds.insert(sizeId);
            parentId = sizeId;
        } else {
            const std::string id = "uncategorized";
            auto& node = m_nodes[id];
            node.folder = {
                id, m_rootId, "Uncategorized", SupplierToolImportFolderKind::Uncategorized, 0, 0};
            root.childIds.insert(id);
            parentId = id;
        }
        m_nodes[parentId].directToolIds.insert(row.geometryId);
    }

    updateCounts(m_rootId);
}

const SupplierToolImportFolder* SupplierToolImportTree::folder(const std::string& folderId) const {
    const auto found = m_nodes.find(folderId);
    return found == m_nodes.end() ? nullptr : &found->second.folder;
}

std::vector<const SupplierToolImportFolder*> SupplierToolImportTree::children(
    const std::string& folderId) const {
    std::vector<const SupplierToolImportFolder*> result;
    const auto found = m_nodes.find(folderId);
    if (found == m_nodes.end())
        return result;
    result.reserve(found->second.childIds.size());
    for (const auto& child : found->second.childIds)
        result.push_back(&m_nodes.at(child).folder);
    std::sort(result.begin(), result.end(), folderLess);
    return result;
}

std::vector<std::string> SupplierToolImportTree::collectToolIds(const std::string& folderId) const {
    const auto found = m_nodes.find(folderId);
    if (found == m_nodes.end())
        return {};

    std::set<std::string> result = found->second.directToolIds;
    for (const auto& child : found->second.childIds) {
        const auto childTools = collectToolIds(child);
        result.insert(childTools.begin(), childTools.end());
    }
    return {result.begin(), result.end()};
}

std::vector<std::string> SupplierToolImportTree::toolIds(const std::string& folderId) const {
    return collectToolIds(folderId);
}

std::vector<std::string> SupplierToolImportTree::matchingToolIds(
    const std::string& folderId,
    const std::vector<SupplierToolImportRow>& rows,
    const std::string& query,
    bool selectableOnly) const {
    const auto branchIds = collectToolIds(folderId);
    std::set<std::string> wanted(branchIds.begin(), branchIds.end());
    std::vector<std::string> result;
    for (const auto& row : rows) {
        if (wanted.count(row.geometryId) == 0 || (selectableOnly && row.alreadyLocal) ||
            !supplierToolImportRowMatches(row, query)) {
            continue;
        }
        result.push_back(row.geometryId);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

SupplierToolImportSelectionState SupplierToolImportTree::selectionState(
    const std::string& folderId,
    const std::vector<SupplierToolImportRow>& rows,
    const SupplierToolImportSelection& selection,
    const std::string& query) const {
    const auto eligible = matchingToolIds(folderId, rows, query, true);
    const auto selected = static_cast<std::size_t>(
        std::count_if(eligible.begin(), eligible.end(), [&selection](const std::string& id) {
            return selection.contains(id);
        }));
    if (selected == 0)
        return SupplierToolImportSelectionState::Unchecked;
    if (selected == eligible.size())
        return SupplierToolImportSelectionState::Checked;
    return SupplierToolImportSelectionState::Mixed;
}

void SupplierToolImportTree::setBranchSelected(const std::string& folderId,
                                               const std::vector<SupplierToolImportRow>& rows,
                                               const std::string& query,
                                               bool selected,
                                               SupplierToolImportSelection& selection) const {
    for (const auto& id : matchingToolIds(folderId, rows, query, true))
        selection.setSelected(id, selected);
}

std::size_t SupplierToolImportTree::updateCounts(const std::string& folderId) {
    auto found = m_nodes.find(folderId);
    if (found == m_nodes.end())
        return 0;
    auto& node = found->second;
    node.folder.directToolCount = node.directToolIds.size();
    node.folder.totalToolCount = node.folder.directToolCount;
    for (const auto& child : node.childIds)
        node.folder.totalToolCount += updateCounts(child);
    return node.folder.totalToolCount;
}

} // namespace dw

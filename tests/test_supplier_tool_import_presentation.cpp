#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>

#include "core/database/supplier_tool_catalog.h"
#include "ui/dialogs/supplier_tool_import_presentation.h"

namespace {

dw::SupplierToolImportRow row(std::string id,
                              std::string name,
                              std::string folder,
                              bool alreadyLocal = false) {
    dw::SupplierToolImportRow value;
    value.geometryId = std::move(id);
    value.displayName = std::move(name);
    value.folderPath = std::move(folder);
    value.toolType = "End Mill";
    value.size = "0.25 in";
    value.flutes = 2;
    value.alreadyLocal = alreadyLocal;
    return value;
}

dw::SupplierToolImportRow treeRow(std::string id,
                                  std::vector<std::string> path,
                                  std::string type = "End Mill",
                                  std::string size = "0.25 in",
                                  bool alreadyLocal = false) {
    auto value = row(std::move(id), "Tool", "", alreadyLocal);
    value.categoryPath = std::move(path);
    value.toolType = std::move(type);
    value.size = std::move(size);
    return value;
}

const dw::SupplierToolImportFolder* childNamed(const dw::SupplierToolImportTree& tree,
                                               const std::string& parentId,
                                               const std::string& label,
                                               dw::SupplierToolImportFolderKind kind) {
    const auto children = tree.children(parentId);
    const auto found = std::find_if(children.begin(), children.end(), [&](const auto* child) {
        return child->label == label && child->kind == kind;
    });
    return found == children.end() ? nullptr : *found;
}

std::vector<std::string> folderIds(const dw::SupplierToolImportTree& tree,
                                   const std::string& parentId) {
    std::vector<std::string> result;
    for (const auto* child : tree.children(parentId)) {
        result.push_back(child->id);
        const auto descendants = folderIds(tree, child->id);
        result.insert(result.end(), descendants.begin(), descendants.end());
    }
    return result;
}

std::size_t descendantFolderCount(const dw::SupplierToolImportTree& tree,
                                  const std::string& parentId) {
    std::size_t count = 0;
    for (const auto* child : tree.children(parentId))
        count += 1 + descendantFolderCount(tree, child->id);
    return count;
}

} // namespace

TEST(SupplierToolImportPresentation, SearchUsesNameFolderTypeSizeAndFlutes) {
    const auto tool = row("g1", "SpeTool W04031", "Imperial / Up-Cut");

    EXPECT_TRUE(dw::supplierToolImportRowMatches(tool, "w04031"));
    EXPECT_TRUE(dw::supplierToolImportRowMatches(tool, "UP-CUT"));
    EXPECT_TRUE(dw::supplierToolImportRowMatches(tool, "end mill"));
    EXPECT_TRUE(dw::supplierToolImportRowMatches(tool, "0.25"));
    EXPECT_TRUE(dw::supplierToolImportRowMatches(tool, "2"));
    EXPECT_FALSE(dw::supplierToolImportRowMatches(tool, "ball nose"));
}

TEST(SupplierToolImportPresentation, SelectVisibleSkipsHiddenAndAlreadyLocalTools) {
    const std::vector<dw::SupplierToolImportRow> rows = {
        row("visible", "Quarter Inch Up-Cut", "Imperial"),
        row("hidden", "Six Millimeter Up-Cut", "Metric"),
        row("existing", "Quarter Inch Down-Cut", "Imperial", true),
    };
    dw::SupplierToolImportSelection selection;

    selection.selectVisible(rows, "quarter");

    EXPECT_TRUE(selection.contains("visible"));
    EXPECT_FALSE(selection.contains("hidden"));
    EXPECT_FALSE(selection.contains("existing"));
    EXPECT_EQ(selection.count(), 1u);
}

TEST(SupplierToolImportPresentation, SelectionTogglesAndClearsByGeometryId) {
    dw::SupplierToolImportSelection selection;

    selection.setSelected("g2", true);
    selection.setSelected("g1", true);
    selection.setSelected("g2", false);

    EXPECT_EQ(selection.geometryIds(), std::vector<std::string>({"g1"}));
    selection.clear();
    EXPECT_EQ(selection.count(), 0u);
}

TEST(SupplierToolImportPresentation, CopyActionUsesPlainSingularAndPluralCopy) {
    EXPECT_EQ(dw::supplierToolImportActionLabel(0), "Copy 0 Tools");
    EXPECT_EQ(dw::supplierToolImportActionLabel(1), "Copy 1 Tool");
    EXPECT_EQ(dw::supplierToolImportActionLabel(12), "Copy 12 Tools");
}

TEST(SupplierToolImportPresentation, TreePreservesSupplierHierarchyAndCounts) {
    const std::vector<dw::SupplierToolImportRow> rows = {
        treeRow("upcut", {"Wood", "End Mills"}),
        treeRow("vbit", {"Wood", "V-Bits"}),
        treeRow("aluminum", {"Metal"}),
    };
    const dw::SupplierToolImportTree tree(rows);

    const auto* root = tree.folder(tree.rootId());
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->label, "All Tools");
    EXPECT_EQ(root->kind, dw::SupplierToolImportFolderKind::AllTools);
    EXPECT_EQ(root->directToolCount, 0u);
    EXPECT_EQ(root->totalToolCount, 3u);

    const auto* wood =
        childNamed(tree, tree.rootId(), "Wood", dw::SupplierToolImportFolderKind::Supplier);
    ASSERT_NE(wood, nullptr);
    EXPECT_EQ(wood->totalToolCount, 2u);
    const auto* endMills =
        childNamed(tree, wood->id, "End Mills", dw::SupplierToolImportFolderKind::Supplier);
    ASSERT_NE(endMills, nullptr);
    EXPECT_EQ(endMills->directToolCount, 1u);
    EXPECT_EQ(tree.toolIds(wood->id), std::vector<std::string>({"upcut", "vbit"}));
}

TEST(SupplierToolImportPresentation, EmptyPathsUseTypeThenSizeFallback) {
    const std::vector<dw::SupplierToolImportRow> rows = {
        treeRow("quarter", {}, "End Mill", "0.25 in"),
        treeRow("half", {}, "End Mill", "0.5 in"),
        treeRow("no-size", {}, "End Mill", ""),
        treeRow("unknown", {}, "Unknown", "0.125 in"),
    };
    const dw::SupplierToolImportTree tree(rows);

    const auto* endMill = childNamed(
        tree, tree.rootId(), "End Mill", dw::SupplierToolImportFolderKind::GeneratedType);
    ASSERT_NE(endMill, nullptr);
    EXPECT_EQ(endMill->totalToolCount, 3u);
    const auto* quarter =
        childNamed(tree, endMill->id, "0.25 in", dw::SupplierToolImportFolderKind::GeneratedSize);
    ASSERT_NE(quarter, nullptr);
    EXPECT_EQ(tree.toolIds(quarter->id), std::vector<std::string>({"quarter"}));

    const auto* missingSize = childNamed(
        tree, endMill->id, "Uncategorized", dw::SupplierToolImportFolderKind::Uncategorized);
    ASSERT_NE(missingSize, nullptr);
    EXPECT_EQ(tree.toolIds(missingSize->id), std::vector<std::string>({"no-size"}));

    const auto* uncategorized = childNamed(
        tree, tree.rootId(), "Uncategorized", dw::SupplierToolImportFolderKind::Uncategorized);
    ASSERT_NE(uncategorized, nullptr);
    EXPECT_EQ(tree.toolIds(uncategorized->id), std::vector<std::string>({"unknown"}));
    EXPECT_EQ(tree.toolIds(tree.rootId()),
              std::vector<std::string>({"half", "no-size", "quarter", "unknown"}));
}

TEST(SupplierToolImportPresentation, FolderIdsAreStableAndSeparatorSafe) {
    std::vector<dw::SupplierToolImportRow> rows = {
        treeRow("slash", {"A/B"}),
        treeRow("nested", {"A", "B"}),
        treeRow("fallback", {}, "A/B", "Small"),
    };
    const dw::SupplierToolImportTree first(rows);
    const auto firstIds = folderIds(first, first.rootId());

    std::reverse(rows.begin(), rows.end());
    const dw::SupplierToolImportTree second(rows);
    EXPECT_EQ(firstIds, folderIds(second, second.rootId()));

    const auto* slash =
        childNamed(first, first.rootId(), "A/B", dw::SupplierToolImportFolderKind::Supplier);
    const auto* nestedA =
        childNamed(first, first.rootId(), "A", dw::SupplierToolImportFolderKind::Supplier);
    const auto* generated =
        childNamed(first, first.rootId(), "A/B", dw::SupplierToolImportFolderKind::GeneratedType);
    ASSERT_NE(slash, nullptr);
    ASSERT_NE(nestedA, nullptr);
    ASSERT_NE(generated, nullptr);
    const auto* nestedB =
        childNamed(first, nestedA->id, "B", dw::SupplierToolImportFolderKind::Supplier);
    ASSERT_NE(nestedB, nullptr);
    EXPECT_NE(slash->id, nestedB->id);
    EXPECT_NE(slash->id, generated->id);
}

TEST(SupplierToolImportPresentation, BranchSelectionIsFilteredAndTriState) {
    auto first = treeRow("first", {"Cutters"});
    first.displayName = "Quarter Inch Up-Cut";
    auto existing = treeRow("existing", {"Cutters"}, "End Mill", "0.25 in", true);
    existing.displayName = "Quarter Inch Down-Cut";
    auto third = treeRow("third", {"Cutters"});
    third.displayName = "Six Millimeter Up-Cut";
    const std::vector<dw::SupplierToolImportRow> rows = {first, existing, third};
    const dw::SupplierToolImportTree tree(rows);
    const auto* cutters =
        childNamed(tree, tree.rootId(), "Cutters", dw::SupplierToolImportFolderKind::Supplier);
    ASSERT_NE(cutters, nullptr);
    dw::SupplierToolImportSelection selection;

    EXPECT_EQ(tree.selectionState(cutters->id, rows, selection),
              dw::SupplierToolImportSelectionState::Unchecked);
    tree.setBranchSelected(cutters->id, rows, "quarter", true, selection);
    EXPECT_EQ(selection.geometryIds(), std::vector<std::string>({"first"}));
    EXPECT_EQ(tree.selectionState(cutters->id, rows, selection, "quarter"),
              dw::SupplierToolImportSelectionState::Checked);
    EXPECT_EQ(tree.selectionState(cutters->id, rows, selection),
              dw::SupplierToolImportSelectionState::Mixed);

    EXPECT_EQ(tree.matchingToolIds(cutters->id, rows, "quarter"),
              (std::vector<std::string>({"existing", "first"})));
    EXPECT_EQ(tree.matchingToolIds(cutters->id, rows, "quarter", true),
              std::vector<std::string>({"first"}));

    tree.setBranchSelected(tree.rootId(), rows, "", true, selection);
    EXPECT_EQ(selection.geometryIds(), (std::vector<std::string>({"first", "third"})));
    EXPECT_EQ(tree.selectionState(tree.rootId(), rows, selection),
              dw::SupplierToolImportSelectionState::Checked);
}

TEST(SupplierToolImportPresentation, SearchIncludesStructuredCategoryPath) {
    auto tool = row("g1", "Cutter", "");
    tool.categoryPath = {"Imperial", "Compression"};

    EXPECT_TRUE(dw::supplierToolImportRowMatches(tool, "compression"));
}

TEST(SupplierToolImportPresentation, SameFolderLabelUnderDifferentParentsStaysSeparate) {
    const std::vector<dw::SupplierToolImportRow> rows = {
        treeRow("imperial", {"Imperial", "End Mills", "1/4 Shank"}),
        treeRow("metric", {"Metric", "End Mills", "1/4 Shank"}),
    };
    const dw::SupplierToolImportTree tree(rows);

    const auto* imperial = childNamed(
        tree, tree.rootId(), "Imperial", dw::SupplierToolImportFolderKind::Supplier);
    const auto* metric = childNamed(
        tree, tree.rootId(), "Metric", dw::SupplierToolImportFolderKind::Supplier);
    ASSERT_NE(imperial, nullptr);
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(tree.toolIds(imperial->id), std::vector<std::string>({"imperial"}));
    EXPECT_EQ(tree.toolIds(metric->id), std::vector<std::string>({"metric"}));
}

TEST(SupplierToolImportPresentation, ClearingFilteredBranchPreservesOtherSelections) {
    auto quarter = treeRow("quarter", {"Imperial"});
    quarter.displayName = "Quarter Inch";
    auto half = treeRow("half", {"Imperial"});
    half.displayName = "Half Inch";
    auto metric = treeRow("metric", {"Metric"});
    metric.displayName = "Six Millimeter";
    const std::vector<dw::SupplierToolImportRow> rows = {quarter, half, metric};
    const dw::SupplierToolImportTree tree(rows);
    const auto* imperial = childNamed(
        tree, tree.rootId(), "Imperial", dw::SupplierToolImportFolderKind::Supplier);
    ASSERT_NE(imperial, nullptr);

    dw::SupplierToolImportSelection selection;
    tree.setBranchSelected(tree.rootId(), rows, "", true, selection);
    tree.setBranchSelected(imperial->id, rows, "quarter", false, selection);

    EXPECT_FALSE(selection.contains("quarter"));
    EXPECT_TRUE(selection.contains("half"));
    EXPECT_TRUE(selection.contains("metric"));
}

TEST(SupplierToolImportPresentation, AllLocalBranchHasNoSelectableTools) {
    const std::vector<dw::SupplierToolImportRow> rows = {
        treeRow("owned-a", {"Owned"}, "End Mill", "0.25 in", true),
        treeRow("owned-b", {"Owned"}, "End Mill", "0.5 in", true),
    };
    const dw::SupplierToolImportTree tree(rows);
    const auto* owned = childNamed(
        tree, tree.rootId(), "Owned", dw::SupplierToolImportFolderKind::Supplier);
    ASSERT_NE(owned, nullptr);
    dw::SupplierToolImportSelection selection;

    EXPECT_TRUE(tree.matchingToolIds(owned->id, rows, "", true).empty());
    tree.setBranchSelected(owned->id, rows, "", true, selection);
    EXPECT_EQ(selection.count(), 0u);
    EXPECT_EQ(tree.selectionState(owned->id, rows, selection),
              dw::SupplierToolImportSelectionState::Unchecked);
}

TEST(SupplierToolImportTreeIntegration, ReconstructsConfiguredSupplierHierarchy) {
    const char* configuredPath = std::getenv("DW_TEST_SUPPLIER_VTDB");
    if (!configuredPath || configuredPath[0] == '\0')
        GTEST_SKIP() << "Set DW_TEST_SUPPLIER_VTDB to exercise a real supplier tree";

    dw::SupplierToolCatalog catalog;
    const auto opened = catalog.open(configuredPath);
    ASSERT_TRUE(opened) << opened.message;

    std::vector<dw::SupplierToolImportRow> rows;
    rows.reserve(catalog.tools().size());
    for (const auto& tool : catalog.tools()) {
        dw::SupplierToolImportRow value;
        value.geometryId = tool.geometryId;
        value.displayName = tool.displayName;
        value.categoryPath = tool.categoryPath;
        rows.push_back(std::move(value));
    }

    const dw::SupplierToolImportTree tree(rows);
    const auto* root = tree.folder(tree.rootId());
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->totalToolCount, catalog.tools().size());
    EXPECT_GT(descendantFolderCount(tree, tree.rootId()), 0u);

    if (const char* expected = std::getenv("DW_TEST_SUPPLIER_EXPECTED_FOLDERS")) {
        EXPECT_EQ(descendantFolderCount(tree, tree.rootId()),
                  static_cast<std::size_t>(std::strtoull(expected, nullptr, 10)));
    }
}

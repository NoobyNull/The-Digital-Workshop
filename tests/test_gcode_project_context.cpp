// Digital Workshop - G-code project context tests

#include <gtest/gtest.h>

#include "core/project/gcode_project_context.h"

namespace {

dw::ProjectOpenItem makeItem(dw::i64 id,
                             dw::ProjectOpenItemType type,
                             const std::string& name) {
    dw::ProjectOpenItem item;
    item.id = id;
    item.projectId = 7;
    item.itemType = type;
    item.status = dw::ProjectOpenItemStatus::Ready;
    item.displayName = name;
    return item;
}

} // namespace

TEST(GCodeProjectContext, BuildsOperationToolAndWarningLines) {
    auto operation = makeItem(10, dw::ProjectOpenItemType::Operation, "Direct Carve");
    operation.status = dw::ProjectOpenItemStatus::Stale;
    operation.intentJson = R"({
        "material_name":"Walnut",
        "stock":{"width_mm":200,"height_mm":300,"thickness_mm":19}
    })";
    operation.snapshotJson = R"({"machine":{"name":"Shapeoko Pro"}})";

    auto gcode = makeItem(11, dw::ProjectOpenItemType::Gcode, "relief_finish.nc");
    gcode.sourceTable = "gcode_files";
    gcode.sourceId = 42;
    gcode.parentItemId = operation.id;
    gcode.snapshotJson = R"({"estimated_time":90})";

    auto firstTool = makeItem(12, dw::ProjectOpenItemType::Tool, "Tool 1");
    firstTool.parentItemId = operation.id;
    firstTool.sourceKey = "gcode_files:42:tool:1";

    auto unrelatedTool = makeItem(13, dw::ProjectOpenItemType::Tool, "Tool 7");
    unrelatedTool.sourceKey = "gcode_files:99:tool:7";

    auto lines = dw::buildGCodeProjectContextLines(
        "Mantel Sign", {gcode, operation, unrelatedTool, firstTool}, 42);

    ASSERT_EQ(lines.size(), 9u);
    EXPECT_EQ(lines[0], "Project: Mantel Sign");
    EXPECT_EQ(lines[1], "G-code: relief_finish.nc");
    EXPECT_EQ(lines[2], "Operation: Direct Carve");
    EXPECT_EQ(lines[3], "Material: Walnut");
    EXPECT_EQ(lines[4], "Stock: 200 x 300 x 19 mm");
    EXPECT_EQ(lines[5], "Machine profile: Shapeoko Pro");
    EXPECT_EQ(lines[6], "Expected runtime: 1.5 min");
    EXPECT_EQ(lines[7], "Required tool: Tool 1");
    EXPECT_EQ(lines[8], "WARNING: Operation 'Direct Carve' is stale");
}

TEST(GCodeProjectContext, ReportsUnlinkedProjectGCode) {
    auto lines = dw::buildGCodeProjectContextLines("Mantel Sign", {}, 42);

    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "Project: Mantel Sign");
    EXPECT_EQ(lines[1], "G-code: not linked to current project");
}

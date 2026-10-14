// Digital Workshop - Project contents adapter tests

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "app/project_contents_adapter.h"

namespace {

using dw::ProjectContentKind;
using dw::ProjectOpenItem;
using dw::ProjectOpenItemStatus;
using dw::ProjectOpenItemType;
namespace workshop = dw::workshop;

constexpr dw::i64 kProject = 7;

ProjectOpenItem item(dw::i64 id,
                     ProjectOpenItemType type,
                     std::string name,
                     std::optional<dw::i64> parent = std::nullopt,
                     ProjectOpenItemStatus status = ProjectOpenItemStatus::Ready) {
    ProjectOpenItem result;
    result.id = id;
    result.projectId = kProject;
    result.itemType = type;
    result.displayName = std::move(name);
    result.parentItemId = parent;
    result.status = status;
    return result;
}

std::vector<std::string> labels(const dw::ProjectContents& rows) {
    std::vector<std::string> result;
    for (const auto& row : rows) result.push_back(row.label);
    return result;
}

} // namespace

TEST(ProjectContentsAdapter, NestsSetupsGCodeAndRunsUnderTheirDesign) {
    const std::vector<ProjectOpenItem> items = {
        item(1, ProjectOpenItemType::Model, "River Sign"),
        item(2, ProjectOpenItemType::Operation, "Top face", 1),
        item(3, ProjectOpenItemType::Gcode, "river_top.nc", 2),
        item(4, ProjectOpenItemType::Job, "Run: river_top.nc", 3),
        item(5, ProjectOpenItemType::Model, "Coaster"),
    };

    const auto rows = dw::makeProjectContents(workshop::ProjectId(kProject), items);

    ASSERT_EQ(rows.size(), 5U);
    EXPECT_EQ(labels(rows),
              (std::vector<std::string>{
                  "River Sign", "Top face", "river_top.nc", "Run: river_top.nc", "Coaster"}));
    EXPECT_FALSE(rows[0].parent.has_value());
    EXPECT_EQ(rows[0].kind, ProjectContentKind::Design);
    EXPECT_EQ(rows[1].kind, ProjectContentKind::Setup);
    EXPECT_EQ(rows[1].parent, workshop::ProjectItemId(1));
    EXPECT_EQ(rows[2].kind, ProjectContentKind::GCode);
    EXPECT_EQ(rows[3].kind, ProjectContentKind::Run);
    EXPECT_FALSE(rows[4].parent.has_value());
}

TEST(ProjectContentsAdapter, ChildrenFollowTheirParentEvenWhenStoredFirst) {
    const std::vector<ProjectOpenItem> items = {
        item(9, ProjectOpenItemType::Operation, "Back face", 2),
        item(2, ProjectOpenItemType::Model, "Panel"),
    };

    const auto rows = dw::makeProjectContents(workshop::ProjectId(kProject), items);

    EXPECT_EQ(labels(rows), (std::vector<std::string>{"Panel", "Back face"}));
    EXPECT_EQ(rows[1].parent, workshop::ProjectItemId(2));
}

TEST(ProjectContentsAdapter, OrphansAndCyclesStayVisibleAtTheRoot) {
    const std::vector<ProjectOpenItem> items = {
        item(1, ProjectOpenItemType::Gcode, "orphan.nc", 99),
        item(2, ProjectOpenItemType::Operation, "Loop A", 3),
        item(3, ProjectOpenItemType::Operation, "Loop B", 2),
    };

    const auto rows = dw::makeProjectContents(workshop::ProjectId(kProject), items);

    ASSERT_EQ(rows.size(), 3U);
    for (const auto& row : rows) EXPECT_FALSE(row.parent.has_value()) << row.label;
}

TEST(ProjectContentsAdapter, DropsForeignProjectItemsAndRetiredTypes) {
    auto foreign = item(1, ProjectOpenItemType::Model, "Someone else's");
    foreign.projectId = kProject + 1;
    const std::vector<ProjectOpenItem> items = {
        foreign,
        item(2, ProjectOpenItemType::Model, "Mine"),
        item(3, ProjectOpenItemType::Cost, "Old estimate"),
        item(4, ProjectOpenItemType::CutPlan, "Old cut plan"),
    };

    const auto rows = dw::makeProjectContents(workshop::ProjectId(kProject), items);

    EXPECT_EQ(labels(rows), (std::vector<std::string>{"Mine"}));
}

TEST(ProjectContentsAdapter, StaleOutputReadsAsOutOfDateAndNeedsAttention) {
    const std::vector<ProjectOpenItem> items = {
        item(1, ProjectOpenItemType::Model, "Sign"),
        item(2,
             ProjectOpenItemType::Gcode,
             "sign.nc",
             1,
             ProjectOpenItemStatus::Stale),
        item(3,
             ProjectOpenItemType::Job,
             "Run: sign.nc",
             2,
             ProjectOpenItemStatus::Stale),
    };

    const auto rows = dw::makeProjectContents(workshop::ProjectId(kProject), items);

    ASSERT_EQ(rows.size(), 3U);
    EXPECT_FALSE(rows[0].needsAttention);
    EXPECT_EQ(rows[1].status, "Out of date");
    EXPECT_TRUE(rows[1].needsAttention);
    EXPECT_EQ(rows[2].status, "Interrupted");
}

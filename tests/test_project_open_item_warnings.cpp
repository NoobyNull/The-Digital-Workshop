// Digital Workshop - Project open item warning tests

#include <gtest/gtest.h>

#include "core/project/project_open_item_warnings.h"

namespace {

dw::ProjectOpenItem makeItem(dw::i64 id,
                             dw::ProjectOpenItemType type,
                             const std::string& name,
                             dw::ProjectOpenItemStatus status) {
    dw::ProjectOpenItem item;
    item.id = id;
    item.itemType = type;
    item.displayName = name;
    item.status = status;
    return item;
}

} // namespace

TEST(ProjectOpenItemWarnings, BuildsWarningsForStaleAndMissingItems) {
    std::vector<dw::ProjectOpenItem> items = {
        makeItem(1, dw::ProjectOpenItemType::Model, "Relief", dw::ProjectOpenItemStatus::Ready),
        makeItem(2, dw::ProjectOpenItemType::Gcode, "relief.nc", dw::ProjectOpenItemStatus::Stale),
        makeItem(3, dw::ProjectOpenItemType::Stock, "Walnut blank",
                 dw::ProjectOpenItemStatus::Missing),
    };

    auto warnings = dw::buildProjectOpenItemWarningLines(items);

    ASSERT_EQ(warnings.size(), 2u);
    EXPECT_EQ(warnings[0], "G-code 'relief.nc' is stale");
    EXPECT_EQ(warnings[1], "Stock 'Walnut blank' is missing");
}

TEST(ProjectOpenItemWarnings, ReturnsEmptyWhenProjectIsCurrent) {
    std::vector<dw::ProjectOpenItem> items = {
        makeItem(1, dw::ProjectOpenItemType::Model, "Relief", dw::ProjectOpenItemStatus::Ready),
        makeItem(2, dw::ProjectOpenItemType::Job, "Run 1", dw::ProjectOpenItemStatus::Complete),
    };

    EXPECT_TRUE(dw::buildProjectOpenItemWarningLines(items).empty());
}

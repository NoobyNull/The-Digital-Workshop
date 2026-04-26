#include <fstream>

#include <gtest/gtest.h>

#include "ui/panels/log_viewer_panel.h"

TEST(LogViewerPanel, ReadsTailFromLogFile) {
    auto path = std::filesystem::temp_directory_path() / "dw-log-viewer-tail.log";
    {
        std::ofstream out(path);
        out << "line 1\nline 2\nline 3\n";
    }

    auto lines = dw::readLogTail(path, 2);

    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "line 2");
    EXPECT_EQ(lines[1], "line 3");
    std::filesystem::remove(path);
}

TEST(LogViewerPanel, FiltersLinesCaseInsensitively) {
    std::vector<std::string> lines{
        "Tagger model_id=1394 retry_view",
        "Thumbnail rendered front",
        "Import done",
    };

    auto filtered = dw::filterLogLines(lines, "FRONT");

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0], "Thumbnail rendered front");
}

TEST(LogViewerPanel, MissingLogReturnsMessage) {
    auto path = std::filesystem::temp_directory_path() / "dw-missing-log-viewer.log";
    std::filesystem::remove(path);

    auto lines = dw::readLogTail(path, 100);

    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("Log file not found"), std::string::npos);
}

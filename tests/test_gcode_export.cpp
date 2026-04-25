#include "core/carve/gcode_export.h"
#include "core/cnc/machine_units.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace dw;
using namespace dw::carve;

namespace {

// Build a simple toolpath for testing
MultiPassToolpath makeTestToolpath()
{
    MultiPassToolpath mp;

    // Finishing pass: retract, rapid to start, cut two points
    mp.finishing.points = {
        {Vec3{0.0f, 0.0f, 5.0f}, true},   // rapid to safe Z
        {Vec3{0.0f, 0.0f, 5.0f}, true},   // rapid above start
        {Vec3{10.0f, 0.0f, 5.0f}, true},  // rapid to XY position
        {Vec3{10.0f, 0.0f, -1.0f}, false}, // plunge cut
        {Vec3{20.0f, 0.0f, -2.0f}, false}, // feed cut
        {Vec3{30.0f, 0.0f, -1.5f}, false}, // feed cut
    };
    mp.finishing.lineCount = 6;
    mp.finishing.estimatedTimeSec = 120.0f;
    mp.finishing.totalDistanceMm = 50.0f;

    mp.totalTimeSec = 120.0f;
    mp.totalLineCount = 6;

    return mp;
}

MultiPassToolpath makeTestToolpathWithClearing()
{
    auto mp = makeTestToolpath();

    mp.clearing.points = {
        {Vec3{0.0f, 0.0f, 5.0f}, true},    // rapid retract
        {Vec3{5.0f, 5.0f, 5.0f}, true},    // rapid to clearing area
        {Vec3{5.0f, 5.0f, -0.5f}, false},  // plunge
        {Vec3{15.0f, 5.0f, -0.5f}, false}, // cut
    };
    mp.clearing.lineCount = 4;
    mp.clearing.estimatedTimeSec = 60.0f;
    mp.clearing.totalDistanceMm = 30.0f;

    mp.totalTimeSec = 180.0f;
    mp.totalLineCount = 10;

    return mp;
}

ToolpathConfig makeTestConfig()
{
    ToolpathConfig cfg;
    cfg.safeZMm = 5.0f;
    cfg.feedRateMmMin = 1000.0f;
    cfg.plungeRateMmMin = 300.0f;
    return cfg;
}

} // anonymous namespace

TEST(GcodeExport, HeaderContainsModelAndTool)
{
    auto tp = makeTestToolpath();
    auto cfg = makeTestConfig();

    std::string gcode = generateGcode(tp, cfg, "dragon.stl", "V-Bit 60deg");

    EXPECT_NE(gcode.find("Direct Carve"), std::string::npos);
    EXPECT_NE(gcode.find("dragon.stl"), std::string::npos);
    EXPECT_NE(gcode.find("V-Bit 60deg"), std::string::npos);
}

TEST(GcodeExport, MetricAbsolute)
{
    auto tp = makeTestToolpath();
    auto cfg = makeTestConfig();

    std::string gcode = generateGcode(tp, cfg, "test", "tool");

    EXPECT_NE(gcode.find("G90 G21"), std::string::npos);
}

TEST(GcodeExport, InchModeConvertsGeneratedCoordinatesAndFeeds)
{
    MultiPassToolpath tp;
    tp.finishing.points = {
        {Vec3{25.4f, 50.8f, -6.35f}, false},
    };
    tp.finishing.lineCount = 1;
    tp.finishing.estimatedTimeSec = 1.0f;

    auto cfg = makeTestConfig();
    cfg.safeZMm = 12.7f;
    cfg.feedRateMmMin = 254.0f;
    cfg.plungeRateMmMin = 127.0f;

    std::string gcode = generateGcode(tp, cfg, "test", "tool", cnc::SendUnits::Inches);

    EXPECT_NE(gcode.find("G90 G20"), std::string::npos);
    EXPECT_NE(gcode.find("(Feed: 10.0 in/min, Plunge: 5.0 in/min)"), std::string::npos);
    EXPECT_NE(gcode.find("(Safe Z: 0.5 in)"), std::string::npos);
    EXPECT_NE(gcode.find("G0 Z0.5"), std::string::npos);
    EXPECT_NE(gcode.find("G1 X1.0 Y2.0 Z-0.25 F10.0"), std::string::npos);
}

TEST(GcodeExport, RapidAndFeed)
{
    auto tp = makeTestToolpath();
    auto cfg = makeTestConfig();

    std::string gcode = generateGcode(tp, cfg, "test", "tool");

    // Rapids use G0
    EXPECT_NE(gcode.find("G0"), std::string::npos);
    // Feed moves use G1
    EXPECT_NE(gcode.find("G1"), std::string::npos);
    // First feed move includes F word
    EXPECT_NE(gcode.find("F1000"), std::string::npos);
}

TEST(GcodeExport, FooterContainsSpindleStopAndEnd)
{
    auto tp = makeTestToolpath();
    auto cfg = makeTestConfig();

    std::string gcode = generateGcode(tp, cfg, "test", "tool");

    // M5 (spindle stop) and M30 (program end) at the end
    auto m5Pos = gcode.rfind("M5");
    auto m30Pos = gcode.rfind("M30");
    EXPECT_NE(m5Pos, std::string::npos);
    EXPECT_NE(m30Pos, std::string::npos);
    EXPECT_LT(m5Pos, m30Pos);
}

TEST(GcodeExport, SafeZRetract)
{
    auto tp = makeTestToolpath();
    auto cfg = makeTestConfig();
    cfg.safeZMm = 8.0f;

    std::string gcode = generateGcode(tp, cfg, "test", "tool");

    // Safe Z retract should appear at start and before footer
    EXPECT_NE(gcode.find("G0 Z8.0"), std::string::npos);
}

TEST(GcodeExport, ClearingPassBeforeFinishing)
{
    auto tp = makeTestToolpathWithClearing();
    auto cfg = makeTestConfig();

    std::string gcode = generateGcode(tp, cfg, "test", "tool");

    auto clearingPos = gcode.find("Clearing pass");
    auto finishingPos = gcode.find("Finishing pass");
    EXPECT_NE(clearingPos, std::string::npos);
    EXPECT_NE(finishingPos, std::string::npos);
    EXPECT_LT(clearingPos, finishingPos);
}

TEST(GcodeExport, NoClearingWhenEmpty)
{
    auto tp = makeTestToolpath();  // No clearing points
    auto cfg = makeTestConfig();

    std::string gcode = generateGcode(tp, cfg, "test", "tool");

    EXPECT_EQ(gcode.find("Clearing pass"), std::string::npos);
    EXPECT_NE(gcode.find("Finishing pass"), std::string::npos);
}

TEST(GcodeExport, FileWritable)
{
    auto tp = makeTestToolpath();
    auto cfg = makeTestConfig();

    std::string tmpPath = std::filesystem::temp_directory_path().string()
                          + "/dw_test_gcode_export.nc";

    bool ok = exportGcode(tmpPath, tp, cfg, "test_model", "test_tool");
    EXPECT_TRUE(ok);

    // Verify file exists and has content
    std::ifstream file(tmpPath);
    EXPECT_TRUE(file.good());

    std::ostringstream contentStream;
    contentStream << file.rdbuf();
    std::string content = contentStream.str();
    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("G90 G21"), std::string::npos);

    // Close before removing — Windows locks open files
    file.close();
    std::filesystem::remove(tmpPath);
}

TEST(GcodeExport, CoordinateFormatting)
{
    MultiPassToolpath tp;
    tp.finishing.points = {
        {Vec3{1.5f, 2.25f, -0.1f}, false},
    };
    tp.finishing.lineCount = 1;
    tp.finishing.estimatedTimeSec = 1.0f;

    auto cfg = makeTestConfig();
    std::string gcode = generateGcode(tp, cfg, "test", "tool");

    // Coordinates should appear with reasonable formatting
    EXPECT_NE(gcode.find("X1.5"), std::string::npos);
    EXPECT_NE(gcode.find("Y2.25"), std::string::npos);
    EXPECT_NE(gcode.find("Z-0.1"), std::string::npos);
}

TEST(GcodeExport, FeedRateOnlyOnFirstFeedMove)
{
    MultiPassToolpath tp;
    tp.finishing.points = {
        {Vec3{0.0f, 0.0f, -1.0f}, false},  // First feed
        {Vec3{1.0f, 0.0f, -1.0f}, false},  // Second feed
    };
    tp.finishing.lineCount = 2;
    tp.finishing.estimatedTimeSec = 1.0f;

    auto cfg = makeTestConfig();
    cfg.feedRateMmMin = 800.0f;

    std::string gcode = generateGcode(tp, cfg, "test", "tool");

    // F should appear exactly once for feed moves
    size_t fCount = 0;
    size_t pos = 0;
    while ((pos = gcode.find(" F", pos)) != std::string::npos) {
        // Only count F on G1 lines (not in header comments)
        auto lineStart = gcode.rfind('\n', pos);
        if (lineStart == std::string::npos) lineStart = 0;
        auto line = gcode.substr(lineStart, pos - lineStart);
        if (line.find("G1") != std::string::npos) {
            ++fCount;
        }
        pos += 2;
    }
    EXPECT_EQ(fCount, 1u);
}

TEST(GcodeExport, DownwardMoveAfterRapidUsesPlungeRateThenCutFeed)
{
    MultiPassToolpath tp;
    tp.finishing.points = {
        {Vec3{0.0f, 0.0f, 5.0f}, true},
        {Vec3{0.0f, 0.0f, -1.0f}, false},
        {Vec3{10.0f, 0.0f, -1.0f}, false},
    };
    tp.finishing.lineCount = 3;
    tp.finishing.estimatedTimeSec = 1.0f;

    auto cfg = makeTestConfig();
    cfg.feedRateMmMin = 1000.0f;
    cfg.plungeRateMmMin = 300.0f;

    std::string gcode = generateGcode(tp, cfg, "test", "tool");

    const auto plunge = gcode.find("G1 X0.0 Y0.0 Z-1.0 F300");
    const auto cut = gcode.find("G1 X10.0 Y0.0 Z-1.0 F1000");
    ASSERT_NE(plunge, std::string::npos);
    ASSERT_NE(cut, std::string::npos);
    EXPECT_LT(plunge, cut);
}

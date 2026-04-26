#include "core/carve/carve_streamer.h"
#include "core/cnc/machine_units.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace dw;
using namespace dw::carve;

namespace {

MultiPassToolpath makeTestToolpath()
{
    MultiPassToolpath mp;

    mp.finishing.points = {
        {Vec3{0.0f, 0.0f, 5.0f}, true},    // rapid
        {Vec3{10.0f, 0.0f, 5.0f}, true},   // rapid
        {Vec3{10.0f, 0.0f, -1.0f}, false},  // feed cut
        {Vec3{20.0f, 0.0f, -2.0f}, false},  // feed cut
    };
    mp.finishing.lineCount = 4;
    mp.finishing.estimatedTimeSec = 60.0f;

    mp.totalTimeSec = 60.0f;
    mp.totalLineCount = 4;

    return mp;
}

MultiPassToolpath makeToolpathWithClearing()
{
    MultiPassToolpath mp;

    mp.clearing.points = {
        {Vec3{0.0f, 0.0f, 5.0f}, true},     // rapid
        {Vec3{5.0f, 5.0f, -0.5f}, false},   // cut
        {Vec3{15.0f, 5.0f, -0.5f}, false},  // cut
    };
    mp.clearing.lineCount = 3;
    mp.clearing.estimatedTimeSec = 30.0f;

    mp.finishing.points = {
        {Vec3{0.0f, 0.0f, 5.0f}, true},     // rapid
        {Vec3{10.0f, 0.0f, -1.0f}, false},  // cut
    };
    mp.finishing.lineCount = 2;
    mp.finishing.estimatedTimeSec = 30.0f;

    mp.totalTimeSec = 60.0f;
    mp.totalLineCount = 5;

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

// Drain all lines from streamer into a vector
std::vector<std::string> drainAll(CarveStreamer& s)
{
    std::vector<std::string> lines;
    while (!s.isComplete()) {
        std::string line = s.nextLine();
        if (line.empty()) break;
        lines.push_back(line);
    }
    return lines;
}

} // anonymous namespace

TEST(CarveStreamer, PreambleFirst)
{
    CarveStreamer s;
    s.start(makeTestToolpath(), makeTestConfig());

    std::string first = s.nextLine();
    EXPECT_NE(first.find("G90"), std::string::npos);
    EXPECT_NE(first.find("G21"), std::string::npos);
}

TEST(CarveStreamer, InchModeConvertsMovesFeedAndRetract)
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

    CarveStreamer s;
    s.start(tp, cfg, cnc::SendUnits::Inches);

    EXPECT_EQ(s.nextLine(), "G90 G20");
    EXPECT_EQ(s.nextLine(), "G1 X1.0 Y2.0 Z-0.25 F10.0");
    EXPECT_EQ(s.nextLine(), "G0 Z0.5");
}

TEST(CarveStreamer, RapidFormat)
{
    CarveStreamer s;
    s.start(makeTestToolpath(), makeTestConfig());

    s.nextLine(); // preamble
    std::string rapid = s.nextLine(); // first finishing point is rapid

    EXPECT_NE(rapid.find("G0"), std::string::npos);
    EXPECT_NE(rapid.find("X0.0"), std::string::npos);
    EXPECT_NE(rapid.find("Z5.0"), std::string::npos);
    // G0 should NOT have F (feed rate)
    EXPECT_EQ(rapid.find("F"), std::string::npos);
}

TEST(CarveStreamer, LinearFormat)
{
    CarveStreamer s;
    s.start(makeTestToolpath(), makeTestConfig());

    s.nextLine(); // preamble
    s.nextLine(); // rapid 1
    s.nextLine(); // rapid 2
    std::string cut = s.nextLine(); // first feed move

    EXPECT_NE(cut.find("G1"), std::string::npos);
    EXPECT_NE(cut.find("F300"), std::string::npos);
}

TEST(CarveStreamer, DownwardMoveAfterRapidUsesPlungeRateThenCutFeed)
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

    CarveStreamer s;
    s.start(tp, cfg);

    EXPECT_EQ(s.nextLine(), "G90 G21");
    EXPECT_EQ(s.nextLine(), "G0 X0.0 Y0.0 Z5.0");
    EXPECT_EQ(s.nextLine(), "G1 X0.0 Y0.0 Z-1.0 F300.0");
    EXPECT_EQ(s.nextLine(), "G1 X10.0 Y0.0 Z-1.0 F1000.0");
}

TEST(CarveStreamer, FeedRateOptimization)
{
    auto tp = makeTestToolpath();
    tp.finishing.points.push_back({Vec3{30.0f, 0.0f, -2.0f}, false});

    CarveStreamer s;
    s.start(tp, makeTestConfig());

    s.nextLine(); // preamble
    s.nextLine(); // rapid 1
    s.nextLine(); // rapid 2
    s.nextLine(); // plunge — has plunge F
    std::string firstCut = s.nextLine();  // first lateral feed — has F
    std::string secondCut = s.nextLine(); // second lateral feed — no F (same rate)

    EXPECT_NE(firstCut.find("F"), std::string::npos);
    EXPECT_EQ(secondCut.find("F"), std::string::npos);
}

TEST(CarveStreamer, ClearingThenFinishing)
{
    CarveStreamer s;
    auto tp = makeToolpathWithClearing();
    s.start(tp, makeTestConfig());

    auto lines = drainAll(s);

    // Find first clearing point (G0 rapid at 5,5) and first finishing point (G0 rapid at 0,0,5)
    // Clearing: rapid to (0,0,5), cut (5,5,-0.5), cut (15,5,-0.5)
    // Finishing: rapid to (0,0,5), cut (10,0,-1)
    // Clearing points should come before finishing points
    int clearingCutIdx = -1;
    int finishingCutIdx = -1;
    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
        const auto& line = lines[static_cast<size_t>(i)];
        if (line.find("X5.0") != std::string::npos &&
            line.find("Y5.0") != std::string::npos &&
            line.find("G1") != std::string::npos) {
            clearingCutIdx = i;
            break;
        }
    }
    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
        const auto& line = lines[static_cast<size_t>(i)];
        if (line.find("X10.0") != std::string::npos &&
            line.find("Z-1.0") != std::string::npos &&
            line.find("G1") != std::string::npos) {
            finishingCutIdx = i;
            break;
        }
    }
    ASSERT_NE(clearingCutIdx, -1) << "Clearing cut not found";
    ASSERT_NE(finishingCutIdx, -1) << "Finishing cut not found";
    EXPECT_LT(clearingCutIdx, finishingCutIdx);
}

TEST(CarveStreamer, ToolChangeBoundaryBeforeFinishing)
{
    CarveStreamer s;
    auto tp = makeToolpathWithClearing();
    tp.requiresToolChange = true;
    tp.clearingToolName = "6mm End Mill";
    tp.finishingToolName = "1/32 TBN";
    s.start(tp, makeTestConfig());

    auto lines = drainAll(s);

    auto findLine = [&](const std::string& needle) {
        for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
            if (lines[static_cast<size_t>(i)].find(needle) != std::string::npos) {
                return i;
            }
        }
        return -1;
    };

    const int toolChange = findLine("Tool change: 6mm End Mill -> 1/32 TBN");
    const int pause = findLine("M0");
    const int finishing = findLine("X10.0 Y0.0 Z-1.0");

    ASSERT_NE(toolChange, -1);
    ASSERT_NE(pause, -1);
    ASSERT_NE(finishing, -1);
    EXPECT_LT(toolChange, pause);
    EXPECT_LT(pause, finishing);
}

TEST(CarveStreamer, PostambleLast)
{
    CarveStreamer s;
    s.start(makeTestToolpath(), makeTestConfig());

    auto lines = drainAll(s);
    ASSERT_GE(lines.size(), 3u);

    // Last 3 lines should be retract, M5, M30
    auto retract = lines[lines.size() - 3];
    auto m5 = lines[lines.size() - 2];
    auto m30 = lines[lines.size() - 1];

    EXPECT_NE(retract.find("G0 Z5.0"), std::string::npos);
    EXPECT_EQ(m5, "M5");
    EXPECT_EQ(m30, "M30");
}

TEST(CarveStreamer, EmptyAfterComplete)
{
    CarveStreamer s;
    s.start(makeTestToolpath(), makeTestConfig());

    drainAll(s);
    EXPECT_TRUE(s.isComplete());

    // Additional calls return empty
    EXPECT_TRUE(s.nextLine().empty());
    EXPECT_TRUE(s.nextLine().empty());
}

TEST(CarveStreamer, ProgressTracking)
{
    CarveStreamer s;
    s.start(makeTestToolpath(), makeTestConfig());

    f32 prevProgress = 0.0f;
    int calls = 0;
    while (!s.isComplete()) {
        std::string line = s.nextLine();
        if (line.empty()) break;
        f32 p = s.progressFraction();
        EXPECT_GE(p, prevProgress) << "Progress decreased at call " << calls;
        prevProgress = p;
        calls++;
    }

    EXPECT_NEAR(s.progressFraction(), 1.0f, 0.01f);
    EXPECT_GT(calls, 0);
}

TEST(CarveStreamer, AbortStopsStream)
{
    CarveStreamer s;
    s.start(makeTestToolpath(), makeTestConfig());

    s.nextLine(); // preamble
    s.nextLine(); // one point

    s.abort();

    EXPECT_TRUE(s.isComplete());
    EXPECT_FALSE(s.isRunning());
    EXPECT_TRUE(s.nextLine().empty());
}

TEST(CarveStreamer, TotalLineCount)
{
    CarveStreamer s;
    auto tp = makeTestToolpath();
    s.start(tp, makeTestConfig());

    // preamble(1) + 4 finishing points + postamble(3) = 8
    EXPECT_EQ(s.totalLines(), 8);

    auto lines = drainAll(s);
    EXPECT_EQ(static_cast<int>(lines.size()), 8);
    EXPECT_EQ(s.currentLine(), 8);
}

TEST(CarveStreamer, TotalLineCountWithClearing)
{
    CarveStreamer s;
    auto tp = makeToolpathWithClearing();
    s.start(tp, makeTestConfig());

    // preamble(1) + 3 clearing + 2 finishing + postamble(3) = 9
    EXPECT_EQ(s.totalLines(), 9);

    auto lines = drainAll(s);
    EXPECT_EQ(static_cast<int>(lines.size()), 9);
}

TEST(CarveStreamer, EmptyToolpath)
{
    CarveStreamer s;
    MultiPassToolpath empty;
    s.start(empty, makeTestConfig());

    EXPECT_TRUE(s.isComplete());
    EXPECT_FALSE(s.isRunning());
    EXPECT_EQ(s.totalLines(), 0);
    EXPECT_TRUE(s.nextLine().empty());
}

TEST(CarveStreamer, PauseReturnsEmpty)
{
    CarveStreamer s;
    s.start(makeTestToolpath(), makeTestConfig());

    s.nextLine(); // preamble
    s.pause();

    EXPECT_TRUE(s.isPaused());
    EXPECT_TRUE(s.nextLine().empty());

    s.resume();
    EXPECT_FALSE(s.isPaused());
    std::string line = s.nextLine();
    EXPECT_FALSE(line.empty());
}

TEST(CarveStreamer, RunningState)
{
    CarveStreamer s;
    EXPECT_FALSE(s.isRunning());

    s.start(makeTestToolpath(), makeTestConfig());
    EXPECT_TRUE(s.isRunning());

    drainAll(s);
    EXPECT_FALSE(s.isRunning());
}

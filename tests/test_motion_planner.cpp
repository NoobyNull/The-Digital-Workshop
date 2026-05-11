// Digital Workshop - Motion Planner / Trapezoidal Time Estimation Tests

#include <gtest/gtest.h>

#include "core/gcode/gcode_analyzer.h"
#include "core/gcode/gcode_parser.h"
#include "core/gcode/machine_profile.h"

#include <cmath>

using namespace dw;
using namespace dw::gcode;

// Helper: analyze a gcode string with the default profile
static Statistics analyzeWithProfile(const std::string& gcode,
                                     const MachineProfile& profile = MachineProfile::defaultProfile()) {
    Parser parser;
    auto program = parser.parse(gcode);
    Analyzer analyzer;
    analyzer.setMachineProfile(profile);
    return analyzer.analyze(program);
}

// Helper: naive time = distance / feedRate (mm/min → minutes)
static f32 naiveTime(f32 distance, f32 feedRate) {
    return distance / feedRate;
}

// --- Preset factories ---

TEST(MachineProfile, DefaultProfile) {
    auto p = MachineProfile::defaultProfile();
    EXPECT_EQ(p.name, "Default");
    EXPECT_GT(p.maxFeedRateX, 0.0f);
    EXPECT_GT(p.accelX, 0.0f);
}

TEST(MachineProfile, Shapeoko4) {
    auto p = MachineProfile::shapeoko4();
    EXPECT_EQ(p.name, "Shapeoko 4 XXL");
    EXPECT_GT(p.maxFeedRateX, 0.0f);
}

TEST(MachineProfile, LongmillMK2) {
    auto p = MachineProfile::longmillMK2();
    EXPECT_EQ(p.name, "Sienci LongMill MK2 30x30");
    EXPECT_GT(p.maxFeedRateX, 0.0f);
}

// --- Trapezoidal motion tests ---

TEST(MotionPlanner, LongTrapezoidalMove) {
    // A long move should have full trapezoid: accel + cruise + decel
    // At 1000 mm/min (~16.67 mm/s) with 200 mm/s^2 accel,
    // distance to reach vMax = v^2/a = 16.67^2/200 = ~1.39mm
    // 1000mm move is >> 1.39mm, so full trapezoid applies
    auto stats = analyzeWithProfile("G1 X1000 F1000\n");

    f32 naive = naiveTime(1000.0f, 1000.0f); // 1.0 min
    // Trapezoidal time must be >= naive (accel/decel adds time)
    EXPECT_GE(stats.estimatedTime, naive);
    // But shouldn't be wildly longer for a long move (within 10%)
    EXPECT_LT(stats.estimatedTime, naive * 1.10f);
    EXPECT_EQ(stats.segmentTimes.size(), 1u);
}

TEST(MotionPlanner, ShortTriangularMove) {
    // Very short move — never reaches commanded feed rate
    // 0.1mm at 5000 mm/min with 200 mm/s^2 accel:
    // d_full = (5000/60)^2 / 200 = 83.33^2/200 = ~34.7mm
    // 0.1mm << 34.7mm → triangle profile
    auto stats = analyzeWithProfile("G1 X0.1 F5000\n");

    f32 naive = naiveTime(0.1f, 5000.0f);
    // Triangle time is significantly longer than naive for short moves
    EXPECT_GT(stats.estimatedTime, naive * 1.5f);
    EXPECT_EQ(stats.segmentTimes.size(), 1u);
}

TEST(MotionPlanner, DiagonalAxisLimiting) {
    // Diagonal move: direction (0.6, 0.8, 0) normalized
    // If maxFeedRateX = maxFeedRateY = 5000, commanded = 5000
    // Y component: 5000 * 0.8 = 4000 < 5000, OK
    // X component: 5000 * 0.6 = 3000 < 5000, OK
    // No limiting needed for equal max rates
    auto stats = analyzeWithProfile("G1 X60 Y80 F5000\n");

    f32 distance = std::sqrt(60.0f * 60.0f + 80.0f * 80.0f); // 100mm
    EXPECT_NEAR(stats.totalPathLength, distance, 0.1f);
    EXPECT_GT(stats.estimatedTime, 0.0f);

    // Now test with Z axis which is slower (3000 mm/min)
    auto statsZ = analyzeWithProfile("G1 X60 Y0 Z80 F5000\n");
    // Z axis limiting should make this slower
    EXPECT_GT(statsZ.estimatedTime, 0.0f);
}

TEST(MotionPlanner, ManyShortSegmentsSlowerThanOneLong) {
    // Many 1mm segments should take longer than one 100mm segment
    // because each short segment must accel from 0 and decel to 0
    std::string manyShort;
    for (int i = 1; i <= 100; ++i) {
        manyShort += "G1 X" + std::to_string(i) + " F3000\n";
    }
    auto statsMany = analyzeWithProfile(manyShort);

    auto statsOne = analyzeWithProfile("G1 X100 F3000\n");

    EXPECT_GT(statsMany.estimatedTime, statsOne.estimatedTime);
    EXPECT_EQ(statsMany.segmentTimes.size(), 100u);
}

TEST(MotionPlanner, RapidUsesRapidRate) {
    // G0 should use the profile's rapidRate, not a feed rate
    auto stats = analyzeWithProfile("G0 X100\n");

    EXPECT_GT(stats.estimatedTime, 0.0f);
    EXPECT_NEAR(stats.rapidPathLength, 100.0f, 0.01f);
    EXPECT_EQ(stats.segmentTimes.size(), 1u);
}

TEST(MotionPlanner, ZeroLengthSegment) {
    // Moving to the same position should have zero time
    auto stats = analyzeWithProfile("G1 X0 Y0 Z0 F1000\n");

    EXPECT_FLOAT_EQ(stats.estimatedTime, 0.0f);
    // Parser may not generate a segment for zero-length move,
    // but if it does, time should be 0
    for (auto t : stats.segmentTimes) {
        EXPECT_FLOAT_EQ(t, 0.0f);
    }
}

TEST(MotionPlanner, TimeAlwaysGreaterThanOrEqualToNaive) {
    // For any move, trapezoidal time >= naive time
    // Test several scenarios
    struct TestCase {
        const char* gcode;
        f32 distance;
        f32 feedRate;
    };

    TestCase cases[] = {
        {"G1 X10 F1000\n", 10.0f, 1000.0f},
        {"G1 X100 F2000\n", 100.0f, 2000.0f},
        {"G1 X500 F5000\n", 500.0f, 5000.0f},
        {"G1 X1 F500\n", 1.0f, 500.0f},
    };

    for (const auto& tc : cases) {
        auto stats = analyzeWithProfile(tc.gcode);
        f32 naive = naiveTime(tc.distance, tc.feedRate);
        EXPECT_GE(stats.estimatedTime + 1e-6f, naive)
            << "Failed for: " << tc.gcode;
    }
}

TEST(MotionPlanner, SegmentTimesParallelToPath) {
    auto gcode = "G0 X10\nG1 X20 F1000\nG1 X30 F2000\n";
    Parser parser;
    auto program = parser.parse(gcode);
    Analyzer analyzer;
    auto stats = analyzer.analyze(program);

    EXPECT_EQ(stats.segmentTimes.size(), program.path.size());
}

TEST(MotionPlanner, DifferentProfilesGiveDifferentTimes) {
    auto gcode = "G1 X100 F3000\n";

    auto statsDefault = analyzeWithProfile(gcode, MachineProfile::defaultProfile());
    auto statsShapeoko = analyzeWithProfile(gcode, MachineProfile::shapeoko4());

    // Shapeoko4 has higher accel (400 vs 200) so should be slightly faster
    EXPECT_NE(statsDefault.estimatedTime, statsShapeoko.estimatedTime);
}

// --- JSON serialization ---

TEST(MachineProfile, JsonRoundTrip) {
    auto original = MachineProfile::shapeoko4();
    original.defaultPlungeRate = 725.0f;
    original.defaultStepdown = 2.25f;
    std::string jsonStr = original.toJsonString();
    auto restored = MachineProfile::fromJsonString(jsonStr);

    EXPECT_EQ(restored.name, original.name);
    EXPECT_FLOAT_EQ(restored.maxFeedRateX, original.maxFeedRateX);
    EXPECT_FLOAT_EQ(restored.maxFeedRateY, original.maxFeedRateY);
    EXPECT_FLOAT_EQ(restored.maxFeedRateZ, original.maxFeedRateZ);
    EXPECT_FLOAT_EQ(restored.accelX, original.accelX);
    EXPECT_FLOAT_EQ(restored.accelY, original.accelY);
    EXPECT_FLOAT_EQ(restored.accelZ, original.accelZ);
    EXPECT_FLOAT_EQ(restored.maxTravelX, original.maxTravelX);
    EXPECT_FLOAT_EQ(restored.maxTravelY, original.maxTravelY);
    EXPECT_FLOAT_EQ(restored.maxTravelZ, original.maxTravelZ);
    EXPECT_FLOAT_EQ(restored.junctionDeviation, original.junctionDeviation);
    EXPECT_FLOAT_EQ(restored.rapidRate, original.rapidRate);
    EXPECT_FLOAT_EQ(restored.defaultFeedRate, original.defaultFeedRate);
    EXPECT_FLOAT_EQ(restored.defaultPlungeRate, original.defaultPlungeRate);
    EXPECT_FLOAT_EQ(restored.defaultStepdown, original.defaultStepdown);

    // builtIn is NOT serialized — it's a runtime-only flag
    EXPECT_FALSE(restored.builtIn);
}

TEST(MachineProfile, JsonMissingFields) {
    // Partial JSON: only name and accelX
    std::string partialJson = R"({"name":"Partial","accelX":999.0})";
    auto p = MachineProfile::fromJsonString(partialJson);

    EXPECT_EQ(p.name, "Partial");
    EXPECT_FLOAT_EQ(p.accelX, 999.0f);
    // All other fields should be defaults
    MachineProfile def;
    EXPECT_FLOAT_EQ(p.maxFeedRateX, def.maxFeedRateX);
    EXPECT_FLOAT_EQ(p.maxFeedRateY, def.maxFeedRateY);
    EXPECT_FLOAT_EQ(p.rapidRate, def.rapidRate);
    EXPECT_FLOAT_EQ(p.defaultFeedRate, def.defaultFeedRate);
    EXPECT_FLOAT_EQ(p.defaultPlungeRate, def.defaultPlungeRate);
    EXPECT_FLOAT_EQ(p.defaultStepdown, def.defaultStepdown);
}

TEST(MachineProfile, JsonInvalidString) {
    // Invalid JSON should return defaults
    auto p = MachineProfile::fromJsonString("not json at all");
    EXPECT_EQ(p.name, "Default");
    EXPECT_FLOAT_EQ(p.maxFeedRateX, 5000.0f);
}

TEST(MachineProfile, JsonStringRoundTrip) {
    // Serialize to string and back (simulates config.ini storage)
    auto original = MachineProfile::longmillMK2();
    std::string jsonStr = original.toJsonString();
    auto restored = MachineProfile::fromJsonString(jsonStr);

    EXPECT_EQ(restored.name, original.name);
    EXPECT_FLOAT_EQ(restored.maxTravelY, original.maxTravelY);
}

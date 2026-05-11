#include <gtest/gtest.h>

#include "core/gcode/gcode_viewport_sampling.h"

TEST(GCodeViewportSampling, UsesEverySegmentBelowLimit)
{
    EXPECT_EQ(dw::gcode::viewportSegmentStride(100, 100), 1u);
    EXPECT_TRUE(dw::gcode::shouldIncludeViewportSegment(37, 100, 1));
}

TEST(GCodeViewportSampling, StridesLargeProgramsUnderLimit)
{
    const std::size_t stride =
        dw::gcode::viewportSegmentStride(9742120, 250000);

    EXPECT_EQ(stride, 39u);
    EXPECT_LE((9742120 + stride - 1) / stride, 250000u);
}

TEST(GCodeViewportSampling, KeepsFirstLastAndStridePoints)
{
    const std::size_t segmentCount = 1000;
    const std::size_t stride = dw::gcode::viewportSegmentStride(segmentCount, 100);

    EXPECT_TRUE(dw::gcode::shouldIncludeViewportSegment(0, segmentCount, stride));
    EXPECT_TRUE(dw::gcode::shouldIncludeViewportSegment(999, segmentCount, stride));
    EXPECT_TRUE(dw::gcode::shouldIncludeViewportSegment(stride, segmentCount, stride));
    EXPECT_FALSE(dw::gcode::shouldIncludeViewportSegment(1, segmentCount, stride));
}

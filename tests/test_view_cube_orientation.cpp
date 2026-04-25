// Digital Workshop - View cube orientation tests

#include <gtest/gtest.h>

#include "core/viewport/view_cube_orientation.h"

TEST(ViewCubeOrientation, TopFaceKeepsNearestRightAngleYaw) {
    auto snap = dw::snapViewCubeOrientation(dw::ViewCubeFace::Top, 181.0f);

    EXPECT_FLOAT_EQ(snap.yawDeg, 180.0f);
    EXPECT_FLOAT_EQ(snap.pitchDeg, 89.0f);
}

TEST(ViewCubeOrientation, BottomFaceKeepsNearestRightAngleYaw) {
    auto snap = dw::snapViewCubeOrientation(dw::ViewCubeFace::Bottom, 226.0f);

    EXPECT_FLOAT_EQ(snap.yawDeg, 270.0f);
    EXPECT_FLOAT_EQ(snap.pitchDeg, -89.0f);
}

TEST(ViewCubeOrientation, RightAngleYawWrapsAtFullTurn) {
    auto snap = dw::snapViewCubeOrientation(dw::ViewCubeFace::Top, 359.0f);

    EXPECT_FLOAT_EQ(snap.yawDeg, 0.0f);
}

TEST(ViewCubeOrientation, SideFacesUseFaceYaw) {
    auto front = dw::snapViewCubeOrientation(dw::ViewCubeFace::Front, 181.0f);
    EXPECT_FLOAT_EQ(front.yawDeg, 0.0f);
    EXPECT_FLOAT_EQ(front.pitchDeg, 0.0f);

    auto back = dw::snapViewCubeOrientation(dw::ViewCubeFace::Back, 181.0f);
    EXPECT_FLOAT_EQ(back.yawDeg, 180.0f);
    EXPECT_FLOAT_EQ(back.pitchDeg, 0.0f);

    auto left = dw::snapViewCubeOrientation(dw::ViewCubeFace::Left, 181.0f);
    EXPECT_FLOAT_EQ(left.yawDeg, 90.0f);
    EXPECT_FLOAT_EQ(left.pitchDeg, 0.0f);

    auto right = dw::snapViewCubeOrientation(dw::ViewCubeFace::Right, 181.0f);
    EXPECT_FLOAT_EQ(right.yawDeg, 270.0f);
    EXPECT_FLOAT_EQ(right.pitchDeg, 0.0f);
}

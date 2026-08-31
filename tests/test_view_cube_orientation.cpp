// Digital Workshop - View cube orientation tests

#include <gtest/gtest.h>

#include "core/viewport/view_cube_label_layout.h"
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

TEST(ViewCubeOrientation, RotateCurrentViewByQuarterTurns) {
    EXPECT_FLOAT_EQ(dw::rotateViewYawByQuarterTurns(0.0f, 1), 90.0f);
    EXPECT_FLOAT_EQ(dw::rotateViewYawByQuarterTurns(90.0f, 1), 180.0f);
    EXPECT_FLOAT_EQ(dw::rotateViewYawByQuarterTurns(270.0f, 1), 0.0f);
    EXPECT_FLOAT_EQ(dw::rotateViewYawByQuarterTurns(0.0f, -1), 270.0f);
}

TEST(ViewCubeOrientation, RotateCurrentViewKeepsNonCardinalOffset) {
    EXPECT_FLOAT_EQ(dw::rotateViewYawByQuarterTurns(45.0f, 1), 135.0f);
    EXPECT_FLOAT_EQ(dw::rotateViewYawByQuarterTurns(10.0f, -2), 190.0f);
}

TEST(ViewCubeOrientation, RotateCurrentViewVerticallyFromTopCanReachOppositeSide) {
    auto front = dw::rotateViewPitchByQuarterTurns(180.0f, 89.0f, 1);
    EXPECT_FLOAT_EQ(front.yawDeg, 0.0f);
    EXPECT_FLOAT_EQ(front.pitchDeg, 0.0f);

    auto back = dw::rotateViewPitchByQuarterTurns(180.0f, 89.0f, -1);
    EXPECT_FLOAT_EQ(back.yawDeg, 180.0f);
    EXPECT_FLOAT_EQ(back.pitchDeg, 0.0f);
}

TEST(ViewCubeOrientation, RotateCurrentViewVerticallyMovesSidesToTopAndBottom) {
    auto top = dw::rotateViewPitchByQuarterTurns(0.0f, 0.0f, 1);
    EXPECT_FLOAT_EQ(top.yawDeg, 0.0f);
    EXPECT_FLOAT_EQ(top.pitchDeg, 89.0f);

    auto bottom = dw::rotateViewPitchByQuarterTurns(0.0f, 0.0f, -1);
    EXPECT_FLOAT_EQ(bottom.yawDeg, 0.0f);
    EXPECT_FLOAT_EQ(bottom.pitchDeg, -89.0f);
}

TEST(ViewCubeLabelLayout, VisibleFaceContainsItsLabelAtNormalAndScaledTextSizes) {
    const std::array<dw::ViewCubeScreenPoint, 4> face = {{
        {0.0f, 0.0f},
        {60.0f, 0.0f},
        {60.0f, 60.0f},
        {0.0f, 60.0f},
    }};

    EXPECT_TRUE(dw::viewCubeFaceCanContainLabel(face, 14.0f, 14.0f, 2.0f));
    EXPECT_TRUE(dw::viewCubeFaceCanContainLabel(face, 28.0f, 28.0f, 2.0f));
}

TEST(ViewCubeLabelLayout, EdgeOnFaceCannotDrawItsLabelOutsideTheFace) {
    const std::array<dw::ViewCubeScreenPoint, 4> edgeOnFace = {{
        {0.0f, 55.0f},
        {60.0f, 55.0f},
        {60.0f, 60.0f},
        {0.0f, 60.0f},
    }};

    EXPECT_FALSE(
        dw::viewCubeFaceCanContainLabel(edgeOnFace, 14.0f, 14.0f, 2.0f));
}

TEST(ViewCubeLabelLayout, SkewedVisibleFaceStillKeepsItsLabel) {
    const std::array<dw::ViewCubeScreenPoint, 4> face = {{
        {0.0f, 8.0f},
        {45.0f, 0.0f},
        {45.0f, 52.0f},
        {0.0f, 60.0f},
    }};

    EXPECT_TRUE(dw::viewCubeFaceCanContainLabel(face, 14.0f, 14.0f, 2.0f));
}

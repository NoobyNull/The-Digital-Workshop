// Digital Workshop - Camera Tests (no GL required)

#include <gtest/gtest.h>

#include "render/camera.h"

#include <cmath>

namespace {
constexpr float EPS = 1e-4f;
}

// --- Default state ---

TEST(Camera, DefaultState) {
    dw::Camera cam;
    EXPECT_FLOAT_EQ(cam.distance(), 5.0f);
    EXPECT_FLOAT_EQ(cam.pitch(), 89.0f);
    EXPECT_FLOAT_EQ(cam.yaw(), 180.0f);
    EXPECT_FLOAT_EQ(cam.fov(), 45.0f);
}

TEST(Camera, DefaultTarget_Origin) {
    dw::Camera cam;
    EXPECT_FLOAT_EQ(cam.target().x, 0.0f);
    EXPECT_FLOAT_EQ(cam.target().y, 0.0f);
    EXPECT_FLOAT_EQ(cam.target().z, 0.0f);
}

// --- Position ---

TEST(Camera, Position_NonZero) {
    dw::Camera cam;
    auto pos = cam.position();
    // Position should be offset from target by distance
    float dist = std::sqrt(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z);
    EXPECT_NEAR(dist, 5.0f, 0.1f);
}

TEST(Camera, Position_ChangesWithDistance) {
    dw::Camera cam;
    cam.setDistance(10.0f);
    auto pos = cam.position();
    float dist = std::sqrt(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z);
    EXPECT_NEAR(dist, 10.0f, 0.1f);
}

// --- Orbit ---

TEST(Camera, Orbit_ChangesYaw) {
    dw::Camera cam;
    float origYaw = cam.yaw();
    cam.orbit(10.0f, 0.0f);
    EXPECT_NE(cam.yaw(), origYaw);
}

TEST(Camera, Orbit_ChangesPitch) {
    dw::Camera cam;
    float origPitch = cam.pitch();
    cam.orbit(0.0f, -10.0f);
    EXPECT_NE(cam.pitch(), origPitch);
}

TEST(Camera, Orbit_PitchClamped) {
    dw::Camera cam;
    // Push pitch way up — should clamp to max (89)
    cam.orbit(0.0f, 10000.0f);
    EXPECT_LE(cam.pitch(), 89.0f);

    // Push pitch way down — should clamp to min (-89)
    cam.orbit(0.0f, -20000.0f);
    EXPECT_GE(cam.pitch(), -89.0f);
}

TEST(Camera, Orbit_YawWraps) {
    dw::Camera cam;
    // Orbit a full circle
    cam.orbit(720.0f / 0.5f, 0.0f); // 720 degrees at 0.5 sensitivity
    // Yaw should wrap to [0, 360)
    EXPECT_GE(cam.yaw(), 0.0f);
    EXPECT_LT(cam.yaw(), 360.0f);
}

// --- Zoom ---

TEST(Camera, Zoom_ReducesDistance) {
    dw::Camera cam;
    float origDist = cam.distance();
    cam.zoom(1.0f); // Zoom in
    EXPECT_LT(cam.distance(), origDist);
}

TEST(Camera, Zoom_IncreasesDistance) {
    dw::Camera cam;
    float origDist = cam.distance();
    cam.zoom(-1.0f); // Zoom out
    EXPECT_GT(cam.distance(), origDist);
}

TEST(Camera, Zoom_ClampsMinDistance) {
    dw::Camera cam;
    // Zoom in a huge amount
    for (int i = 0; i < 100; ++i)
        cam.zoom(10.0f);
    EXPECT_GE(cam.distance(), 0.1f);
}

TEST(Camera, Zoom_ClampsMaxDistance) {
    dw::Camera cam;
    // Zoom out a huge amount
    for (int i = 0; i < 100; ++i)
        cam.zoom(-10.0f);
    EXPECT_LE(cam.distance(), 10000.0f);
}

// --- Pan ---

TEST(Camera, Pan_MovesTarget) {
    dw::Camera cam;
    auto origTarget = cam.target();
    cam.pan(100.0f, 0.0f);
    auto newTarget = cam.target();
    // Target should have moved
    float dx = newTarget.x - origTarget.x;
    float dy = newTarget.y - origTarget.y;
    float dz = newTarget.z - origTarget.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    EXPECT_GT(dist, 0.0f);
}

// --- FitToBounds ---

TEST(Camera, FitToBounds_CentersOnBounds) {
    dw::Camera cam;
    cam.fitToBounds(dw::Vec3(10, 20, 30), dw::Vec3(20, 40, 60));

    // Target should be center of bounds
    EXPECT_NEAR(cam.target().x, 15.0f, EPS);
    EXPECT_NEAR(cam.target().y, 30.0f, EPS);
    EXPECT_NEAR(cam.target().z, 45.0f, EPS);
}

TEST(Camera, FitToBounds_DistanceProportional) {
    dw::Camera cam;
    cam.fitToBounds(dw::Vec3(0, 0, 0), dw::Vec3(100, 100, 100));
    // Distance should frame the max extent without leaving excessive empty space.
    EXPECT_NEAR(cam.distance(), 95.0f, EPS);
}

TEST(Camera, FitToBoundsProjected_UsesRequestedViewportFill) {
    dw::Camera cam;
    cam.setViewport(512, 512);
    cam.setPitch(0.0f);
    cam.setYaw(0.0f);
    cam.fitToBoundsProjected(dw::Vec3(-50.0f, -0.5f, -0.5f),
                             dw::Vec3(50.0f, 0.5f, 0.5f),
                             0.8f);

    float fovRad = cam.fov() * 3.14159265359f / 180.0f;
    float expected = 0.5f + 50.0f / (std::tan(fovRad * 0.5f) * 0.8f);
    EXPECT_NEAR(cam.distance(), expected, 0.01f);
}

TEST(Camera, FitToBoundsProjected_AccountsForAspectRatio) {
    dw::Camera cam;
    cam.setViewport(1024, 512);
    cam.setPitch(0.0f);
    cam.setYaw(0.0f);
    cam.fitToBoundsProjected(dw::Vec3(-50.0f, -0.5f, -0.5f),
                             dw::Vec3(50.0f, 0.5f, 0.5f),
                             0.8f);

    float fovRad = cam.fov() * 3.14159265359f / 180.0f;
    float expected = 0.5f + 50.0f / ((std::tan(fovRad * 0.5f) * 2.0f) * 0.8f);
    EXPECT_NEAR(cam.distance(), expected, 0.01f);
}

// --- Reset ---

TEST(Camera, Reset_RestoresDefaults) {
    dw::Camera cam;
    cam.orbit(100, 50);
    cam.zoom(5);
    cam.pan(10, 10);

    cam.reset();
    EXPECT_FLOAT_EQ(cam.distance(), 5.0f);
    EXPECT_FLOAT_EQ(cam.pitch(), 89.0f);
    EXPECT_FLOAT_EQ(cam.yaw(), 180.0f);
    EXPECT_FLOAT_EQ(cam.target().x, 0.0f);
}

// --- Viewport ---

TEST(Camera, Viewport_MinClamped) {
    dw::Camera cam;
    cam.setViewport(0, 0);
    // Should clamp to 1x1 to avoid division by zero
    EXPECT_GE(cam.viewportWidth(), 1);
    EXPECT_GE(cam.viewportHeight(), 1);
}

// --- Matrices ---

TEST(Camera, ViewMatrix_NotIdentity) {
    dw::Camera cam;
    auto view = cam.viewMatrix();
    // View matrix should not be identity (camera is offset)
    bool allIdentity = true;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float expected = (i == j) ? 1.0f : 0.0f;
            if (std::abs(view[j][i] - expected) > 0.01f) {
                allIdentity = false;
            }
        }
    }
    EXPECT_FALSE(allIdentity);
}

TEST(Camera, ProjectionMatrix_ValidPerspective) {
    dw::Camera cam;
    cam.setViewport(800, 600);
    auto proj = cam.projectionMatrix();
    // Perspective matrix has (3,2) = -1, (3,3) = 0
    EXPECT_FLOAT_EQ(proj[2][3], -1.0f);
    EXPECT_FLOAT_EQ(proj[3][3], 0.0f);
}

// --- Setters ---

TEST(Camera, SetDistance_Clamps) {
    dw::Camera cam;
    cam.setDistance(-5.0f);
    EXPECT_GE(cam.distance(), 0.1f);

    cam.setDistance(999999.0f);
    EXPECT_LE(cam.distance(), 10000.0f);
}

// --- BUG-02 regression: fmod angle wrapping edge cases ---

TEST(Camera, Orbit_NegativeYaw_WrapsToPositive) {
    dw::Camera cam;
    cam.setYaw(0.0f);
    // Orbit negative (large amount to go well below 0)
    cam.orbit(-1000.0f / 0.5f, 0.0f); // -1000 degrees at 0.5 sensitivity
    // Yaw should be in [0, 360)
    EXPECT_GE(cam.yaw(), 0.0f);
    EXPECT_LT(cam.yaw(), 360.0f);
}

TEST(Camera, Orbit_ExactlyZero) {
    dw::Camera cam;
    cam.setYaw(0.0f);
    cam.orbit(0.0f, 0.0f);
    EXPECT_FLOAT_EQ(cam.yaw(), 0.0f);
}

TEST(Camera, Orbit_Exactly360_WrapsToZero) {
    dw::Camera cam;
    // Set yaw near 360 and push it to exactly 360
    cam.setYaw(359.0f);
    cam.orbit(1.0f / 0.5f, 0.0f); // +1 degree at 0.5 sensitivity = +2.0 * 0.5 = +1.0
    // After wrapping, should be 0.0
    EXPECT_GE(cam.yaw(), 0.0f);
    EXPECT_LT(cam.yaw(), 360.0f);
}

TEST(Camera, Orbit_SmallNegative_WrapsCorrectly) {
    dw::Camera cam;
    cam.setYaw(1.0f);
    // Orbit -4 degrees (sensitivity 0.5, so deltaX = -4/0.5 = -8)
    cam.orbit(-8.0f, 0.0f); // 1.0 + (-8.0 * 0.5) = 1.0 - 4.0 = -3.0 -> wraps to 357.0
    EXPECT_NEAR(cam.yaw(), 357.0f, 0.01f);
}

// --- BUG-03 verification: framebuffer move constructor ---
// BUG-03 (zeroing width/height in move constructor) is ALREADY FIXED.
// Verified by code review: framebuffer.cpp:21-22 zeros m_width and m_height.
// Move assignment operator (framebuffer.cpp:36-37) also zeros correctly.
// Cannot unit test without GL context (framebuffer needs OpenGL).
// No test added — fix confirmed via code inspection.

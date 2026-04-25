#pragma once

#include <algorithm>

#include "../core/types.h"

namespace dw {

// Orbit camera for 3D viewport
class Camera {
  public:
    Camera();

    // Get matrices
    Mat4 viewMatrix() const;
    Mat4 projectionMatrix() const;
    Mat4 viewProjectionMatrix() const;

    // Camera manipulation
    void orbit(f32 deltaX, f32 deltaY); // Rotate around target
    void pan(f32 deltaX, f32 deltaY);   // Move target in view plane
    void zoom(f32 delta);               // Dolly in/out

    // Reset to default view
    void reset();
    void fitToBounds(const Vec3& min, const Vec3& max);

    // Viewport
    void setViewport(int width, int height);
    int viewportWidth() const { return m_viewportWidth; }
    int viewportHeight() const { return m_viewportHeight; }

    // Properties
    const Vec3& target() const { return m_target; }
    void setTarget(const Vec3& target) {
        m_target = target;
        updateVectors();
    }

    f32 distance() const { return m_distance; }
    void setDistance(f32 distance);

    f32 pitch() const { return m_pitch; }
    void setPitch(f32 pitch) {
        m_pitch = std::clamp(pitch, m_minPitch, m_maxPitch);
        updateVectors();
    }
    f32 yaw() const { return m_yaw; }
    void setYaw(f32 yaw) {
        m_yaw = yaw;
        updateVectors();
    }

    f32 fov() const { return m_fov; }
    void setFov(f32 fov) { m_fov = fov; }

    f32 farPlane() const { return m_farPlane; }
    void setFarPlane(f32 fp) { m_farPlane = fp; }

    // Get camera position
    Vec3 position() const;

  private:
    f32 aspectRatio() const;
    void updateVectors();

    Vec3 m_target{0.0f, 0.0f, 0.0f};
    f32 m_distance = 5.0f;
    f32 m_pitch = 89.0f;  // Degrees
    f32 m_yaw = 180.0f;   // Degrees

    f32 m_fov = 45.0f; // Degrees
    f32 m_nearPlane = 0.1f;
    f32 m_farPlane = 1000.0f;

    int m_viewportWidth = 1;
    int m_viewportHeight = 1;

    f32 m_orbitSensitivity = 0.5f;
    f32 m_panSensitivity = 0.01f;
    f32 m_zoomSensitivity = 0.1f;

    f32 m_minDistance = 0.1f;
    f32 m_maxDistance = 10000.0f;
    f32 m_minPitch = -89.0f;
    f32 m_maxPitch = 89.0f;

    // Cached derived values (updated by updateVectors)
    Vec3 m_cachedPosition{0.0f, 0.0f, 0.0f};

    // Stored bounds for reset
    Vec3 m_lastBoundsCenter{0.0f, 0.0f, 0.0f};
    f32 m_lastBoundsExtent = 0.0f;
    bool m_hasBounds = false;
};

} // namespace dw

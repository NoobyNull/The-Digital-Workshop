#include "camera.h"

#include <algorithm>
#include <cmath>

namespace dw {

namespace {
constexpr f32 PI = 3.14159265359f;
constexpr f32 DEG_TO_RAD = PI / 180.0f;
constexpr f32 DEFAULT_DISTANCE = 5.0f;
constexpr f32 DEFAULT_PITCH = 89.0f;
constexpr f32 DEFAULT_YAW = 180.0f;
constexpr f32 FIT_DISTANCE_PADDING = 0.95f;

f32 fitDistanceForExtent(f32 maxExtent) {
    return std::max(maxExtent * FIT_DISTANCE_PADDING, DEFAULT_DISTANCE);
}
} // namespace

Camera::Camera() {
    updateVectors();
}

Mat4 Camera::viewMatrix() const {
    Vec3 pos = position();
    return glm::lookAt(pos, m_target, Vec3{0.0f, 1.0f, 0.0f});
}

Mat4 Camera::projectionMatrix() const {
    return glm::perspective(m_fov * DEG_TO_RAD, aspectRatio(), m_nearPlane, m_farPlane);
}

Mat4 Camera::viewProjectionMatrix() const {
    return projectionMatrix() * viewMatrix();
}

void Camera::orbit(f32 deltaX, f32 deltaY) {
    m_yaw += deltaX * m_orbitSensitivity;
    m_pitch += deltaY * m_orbitSensitivity;

    // Clamp pitch
    m_pitch = std::clamp(m_pitch, m_minPitch, m_maxPitch);

    // Wrap yaw to [0, 360)
    m_yaw = std::fmod(m_yaw, 360.0f);
    if (m_yaw < 0.0f)
        m_yaw += 360.0f;

    updateVectors();
}

void Camera::pan(f32 deltaX, f32 deltaY) {
    f32 yawRad = m_yaw * DEG_TO_RAD;
    f32 pitchRad = m_pitch * DEG_TO_RAD;

    // Calculate right and up vectors in world space
    Vec3 forward{std::sin(yawRad) * std::cos(pitchRad),
                 std::sin(pitchRad),
                 std::cos(yawRad) * std::cos(pitchRad)};
    Vec3 right = glm::normalize(glm::cross(Vec3{0.0f, 1.0f, 0.0f}, forward));
    Vec3 up = glm::normalize(glm::cross(forward, right));

    f32 panScale = m_distance * m_panSensitivity;
    m_target = m_target + right * (-deltaX * panScale) + up * (deltaY * panScale);
    updateVectors();
}

void Camera::zoom(f32 delta) {
    m_distance *= (1.0f - delta * m_zoomSensitivity);
    m_distance = std::clamp(m_distance, m_minDistance, m_maxDistance);
    updateVectors();
}

void Camera::reset() {
    // Restore default orientation
    m_pitch = DEFAULT_PITCH;
    m_yaw = DEFAULT_YAW;

    // If we have stored bounds from a previous fitToBounds, re-fit
    // Otherwise reset to default view
    if (m_hasBounds) {
        m_target = m_lastBoundsCenter;
        m_distance = fitDistanceForExtent(m_lastBoundsExtent);
    } else {
        m_target = Vec3{0.0f, 0.0f, 0.0f};
        m_distance = DEFAULT_DISTANCE;
    }
    updateVectors();
}

void Camera::fitToBounds(const Vec3& min, const Vec3& max) {
    Vec3 center{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f};

    Vec3 size = max - min;
    f32 maxExtent = std::max({size.x, size.y, size.z});

    // Store bounds for reset
    m_lastBoundsCenter = center;
    m_lastBoundsExtent = maxExtent;
    m_hasBounds = true;

    // Pan to center and zoom to fit — preserve current orientation
    m_target = center;
    m_distance = fitDistanceForExtent(maxExtent);

    updateVectors();
}

void Camera::setViewport(int width, int height) {
    m_viewportWidth = std::max(1, width);
    m_viewportHeight = std::max(1, height);
}

f32 Camera::aspectRatio() const {
    return static_cast<f32>(m_viewportWidth) / static_cast<f32>(m_viewportHeight);
}

void Camera::setDistance(f32 distance) {
    m_distance = std::clamp(distance, m_minDistance, m_maxDistance);
    updateVectors();
}

Vec3 Camera::position() const {
    return m_cachedPosition;
}

void Camera::updateVectors() {
    f32 yawRad = m_yaw * DEG_TO_RAD;
    f32 pitchRad = m_pitch * DEG_TO_RAD;

    Vec3 offset{std::sin(yawRad) * std::cos(pitchRad) * m_distance,
                std::sin(pitchRad) * m_distance,
                std::cos(yawRad) * std::cos(pitchRad) * m_distance};

    m_cachedPosition = m_target + offset;

    // Keep clip planes proportional to distance so zooming never causes clipping
    m_nearPlane = m_distance * 0.01f;
    m_farPlane = m_distance * 100.0f;
}

} // namespace dw

#include "viewport_panel.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <imgui.h>

#include "../../core/config/config.h"
#include "../../core/mesh/mesh.h"
#include "../../core/mesh/mesh_repair.h"

namespace dw {

namespace {
constexpr f32 DEFAULT_MODEL_YAW = 180.0f;
constexpr f32 DEFAULT_MODEL_PITCH = 89.0f;
constexpr f32 MAX_RESTORED_PITCH = 80.0f;
constexpr f32 FIT_DISTANCE_PADDING = 0.95f;
constexpr f32 MIN_SAVED_DISTANCE_RATIO = 0.5f;
constexpr f32 MAX_SAVED_DISTANCE_RATIO = 1.75f;

bool finiteVec3(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

f32 expectedFitDistance(const AABB& bounds) {
    Vec3 size = bounds.max - bounds.min;
    f32 maxExtent = std::max({size.x, size.y, size.z});
    return std::max(maxExtent * FIT_DISTANCE_PADDING, 5.0f);
}

bool usableSavedCamera(const CameraState& state, const AABB& bounds) {
    if (!std::isfinite(state.distance) || state.distance <= 0.0f ||
        !std::isfinite(state.pitch) || !std::isfinite(state.yaw) ||
        !finiteVec3(state.target) || std::abs(state.pitch) > MAX_RESTORED_PITCH) {
        return false;
    }

    f32 fitDistance = expectedFitDistance(bounds);
    return state.distance >= fitDistance * MIN_SAVED_DISTANCE_RATIO &&
           state.distance <= fitDistance * MAX_SAVED_DISTANCE_RATIO;
}
} // namespace

ViewportPanel::ViewportPanel() : Panel("Viewport") {
    m_renderer.initialize();
    m_camera.reset();
}

Vec3 ViewportPanel::toolColor(int toolNum) {
    static const Vec3 palette[] = {
        {0.2f, 0.6f, 1.0f},  // T1: Blue
        {1.0f, 0.3f, 0.3f},  // T2: Red
        {0.3f, 0.9f, 0.3f},  // T3: Green
        {1.0f, 0.7f, 0.1f},  // T4: Orange
        {0.8f, 0.3f, 0.9f},  // T5: Purple
        {0.1f, 0.9f, 0.9f},  // T6: Cyan
        {0.9f, 0.9f, 0.2f},  // T7: Yellow
        {1.0f, 0.5f, 0.7f},  // T8: Pink
    };
    int idx = (toolNum > 0 ? toolNum - 1 : 0) % kNumToolColors;
    return palette[idx];
}

void ViewportPanel::render() {
    if (!m_open) {
        return;
    }

    // Lazy initialize context menu entries on first render
    if (m_contextMenuManager != nullptr) {
        static bool entriesRegistered = false;
        if (!entriesRegistered) {
            registerContextMenuEntries();
            entriesRegistered = true;
        }
    }

    applyMinSize(20, 10);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin(m_title.c_str(),
                     &m_open,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        renderToolbar();
        renderViewport();
    }
    ImGui::End();

    ImGui::PopStyleVar();
}

void ViewportPanel::setPresentationIdentity(viewport::PresentationIdentity identity) {
    m_presentationIdentity = std::move(identity);
}

void ViewportPanel::setMesh(MeshPtr mesh) {
    clearFitParams();
    m_mesh = mesh;

    if (m_gpuMesh.vao != 0) {
        m_gpuMesh.destroy();
    }

    if (m_mesh && m_mesh->isValid()) {
        f32 yaw = 0.0f;
        if (Config::instance().getAutoOrient()) {
            yaw = m_mesh->autoOrient();
        }

        m_gpuMesh = m_renderer.uploadMesh(*m_mesh);
        fitToModel();

        if (Config::instance().getAutoOrient()) {
            (void)yaw;
            m_camera.setYaw(DEFAULT_MODEL_YAW);
            m_camera.setPitch(DEFAULT_MODEL_PITCH);
        }
    }

    // Invalidate ViewCube cache (camera may have changed)
    m_viewCubeCache.valid = false;
}

void ViewportPanel::setPreOrientedMesh(MeshPtr mesh,
                                       f32 orientYaw,
                                       std::optional<CameraState> savedCamera) {
    clearFitParams();
    m_mesh = mesh;

    if (m_gpuMesh.vao != 0) {
        m_gpuMesh.destroy();
    }

    if (m_mesh && m_mesh->isValid()) {
        m_gpuMesh = m_renderer.uploadMesh(*m_mesh);

        const auto& bounds = m_mesh->bounds();
        m_camera.setTargetToBoundsCenter(bounds.min, bounds.max);

        if (savedCamera && usableSavedCamera(*savedCamera, bounds)) {
            // Restore viewing angle but keep bounding box center as focal point
            m_camera.setYaw(savedCamera->yaw);
            m_camera.setPitch(savedCamera->pitch);
            m_camera.setDistance(savedCamera->distance);
        } else {
            fitToModel();
            if (m_mesh->wasAutoOriented()) {
                (void)orientYaw;
                m_camera.setYaw(DEFAULT_MODEL_YAW);
                m_camera.setPitch(DEFAULT_MODEL_PITCH);
            }
        }
    }

    m_viewCubeCache.valid = false;
}

CameraState ViewportPanel::getCameraState() const {
    CameraState state;
    state.distance = m_camera.distance();
    state.pitch = m_camera.pitch();
    state.yaw = m_camera.yaw();
    state.target = m_camera.target();
    return state;
}

void ViewportPanel::restoreCameraState(const CameraState& state) {
    m_camera.setTarget(state.target);
    m_camera.setDistance(state.distance);
    m_camera.setYaw(state.yaw);
    m_camera.setPitch(state.pitch);
    m_viewCubeCache.valid = false;
}

void ViewportPanel::setFitParams(const carve::FitParams& params,
                                  const Vec3& modelBoundsMin,
                                  const Vec3& modelBoundsMax,
                                  const carve::StockDimensions& stock) {
    f32 modelExtX = modelBoundsMax.x - modelBoundsMin.x;
    f32 modelExtY = modelBoundsMax.y - modelBoundsMin.y;
    f32 modelExtZ = modelBoundsMax.z - modelBoundsMin.z;
    f32 depth = (params.depthMm > 0.0f) ? params.depthMm : modelExtZ * params.scale;

    f32 sx = (modelExtX > 1e-6f) ? params.scale : 1.0f;
    f32 sy = (modelExtY > 1e-6f) ? params.scale : 1.0f;
    f32 sz = (modelExtZ > 1e-6f) ? (depth / modelExtZ) : 1.0f;

    f32 tx = params.offsetX - modelBoundsMin.x * sx;
    f32 ty = params.offsetY - modelBoundsMin.y * sy;
    (void)stock;
    f32 tz = -depth - modelBoundsMin.z * sz;

    // Build transform in G-code space (Z-up)
    Mat4 fitMat(1.0f);
    fitMat[0][0] = sx;
    fitMat[1][1] = sy;
    fitMat[2][2] = sz;
    fitMat[3][0] = tx;
    fitMat[3][1] = ty;
    fitMat[3][2] = tz;

    // Apply Y<->Z swap for renderer (G-code Z-up -> renderer Y-up)
    // Same swap used in buildGCodeGeometry: G-code (x,y,z) -> renderer (x,z,y)
    Mat4 swapYZ(0.0f);
    swapYZ[0][0] = 1.0f;  // X stays X
    swapYZ[1][2] = 1.0f;  // renderer Y = G-code Z
    swapYZ[2][1] = 1.0f;  // renderer Z = G-code Y
    swapYZ[3][3] = 1.0f;

    m_modelMatrix = swapYZ * fitMat;
    m_hasFitParams = true;

    // Update camera target to transformed bounding box center
    if (m_mesh) {
        Vec3 center = m_mesh->bounds().center();
        Vec4 tc = m_modelMatrix * Vec4(center.x, center.y, center.z, 1.0f);
        m_camera.setTarget(Vec3(tc.x, tc.y, tc.z));
    }

    // Store for alignment validation
    m_fitParams = params;
    m_fitBoundsMin = modelBoundsMin;
    m_fitBoundsMax = modelBoundsMax;
    m_fitStock = stock;
    m_alignmentDirty = true;
}

void ViewportPanel::clearFitParams() {
    m_modelMatrix = Mat4(1.0f);
    m_hasFitParams = false;
    m_alignmentStatus = AlignmentStatus::Unknown;
}

void ViewportPanel::clearMesh() {
    m_mesh = nullptr;
    if (m_gpuMesh.vao != 0) {
        m_gpuMesh.destroy();
    }

    clearFitParams();

    // Invalidate ViewCube cache
    m_viewCubeCache.valid = false;
}

void ViewportPanel::setToolpathMesh(MeshPtr toolpathMesh) {
    m_toolpathMesh = toolpathMesh;

    if (m_gpuToolpath.vao != 0) {
        m_gpuToolpath.destroy();
    }

    if (m_toolpathMesh && m_toolpathMesh->isValid()) {
        m_gpuToolpath = m_renderer.uploadMesh(*m_toolpathMesh);

        // Auto-fit camera to toolpath bounds if no mesh is currently displayed
        if (!m_mesh) {
            const auto& bounds = m_toolpathMesh->bounds();
            m_camera.fitToBounds(bounds.min, bounds.max);
            m_viewCubeCache.valid = false;
        }
    }
}

void ViewportPanel::clearToolpathMesh() {
    m_toolpathMesh = nullptr;
    if (m_gpuToolpath.vao != 0) {
        m_gpuToolpath.destroy();
    }
}

void ViewportPanel::resetView() {
    m_camera.reset();

    // Reset light to defaults
    auto& rs = m_renderer.settings();
    rs.lightDir = Vec3{-0.5f, -1.0f, -0.3f};
    rs.lightColor = Vec3{1.0f, 1.0f, 1.0f};

    // Persist reset light settings
    auto& cfg = Config::instance();
    cfg.setRenderLightDir(rs.lightDir);
    cfg.setRenderLightColor(rs.lightColor);
    cfg.save();

    // Invalidate ViewCube cache (camera changed)
    m_viewCubeCache.valid = false;
}

void ViewportPanel::fitToModel() {
    if (m_mesh && m_mesh->isValid()) {
        const auto& bounds = m_mesh->bounds();
        m_camera.fitToBounds(bounds.min, bounds.max);
    }

    // Invalidate ViewCube cache (camera may have changed)
    m_viewCubeCache.valid = false;
}

bool ViewportPanel::hasValidModel() const {
    return m_mesh != nullptr && m_mesh->isValid();
}

bool ViewportPanel::recalculateModelNormals() {
    if (!hasValidModel()) {
        return false;
    }

    auto result = mesh_repair::recalculateNormals(*m_mesh);
    if (!result.repaired) {
        return false;
    }

    if (m_gpuMesh.vao != 0) {
        m_gpuMesh.destroy();
    }
    m_gpuMesh = m_renderer.uploadMesh(*m_mesh);
    return true;
}

} // namespace dw

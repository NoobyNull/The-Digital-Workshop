#include "viewport_panel.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>

#include <imgui.h>
#include <imgui_internal.h>

#include "../../core/config/config.h"
#include "../../core/config/input_binding.h"
#include "../../core/coordinate_utils.h"
#include "../../core/gcode/gcode_viewport_sampling.h"
#include "../../core/mesh/mesh.h"
#include "../../core/mesh/mesh_repair.h"
#include "../../core/viewport/view_cube_orientation.h"
#include "../../render/gl_utils.h"
#include "../context_menu_manager.h"
#include "../icons.h"
#include "../ui_colors.h"

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

void ViewportPanel::setMesh(MeshPtr mesh) {
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
    m_mesh = mesh;

    if (m_gpuMesh.vao != 0) {
        m_gpuMesh.destroy();
    }

    if (m_mesh && m_mesh->isValid()) {
        m_gpuMesh = m_renderer.uploadMesh(*m_mesh);

        // Always target the bounding box center
        const auto& bounds = m_mesh->bounds();
        m_camera.setTarget(bounds.center());

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

void ViewportPanel::registerContextMenuEntries() {
    if (m_contextMenuManager == nullptr) {
        return;
    }

    std::vector<ContextMenuEntry> entries;

    // Camera Controls
    ContextMenuEntry resetViewEntry;
    resetViewEntry.label = "Reset View";
    resetViewEntry.action = [this]() {
        resetView();
    };
    entries.push_back(resetViewEntry);

    ContextMenuEntry fitEntry;
    fitEntry.label = "Fit to Model";
    fitEntry.action = [this]() {
        fitToModel();
    };
    entries.push_back(fitEntry);

    entries.push_back(ContextMenuEntry::separator());

    // Standard View Directions
    ContextMenuEntry frontEntry;
    frontEntry.label = "Front";
    frontEntry.action = [this]() {
        snapCameraToView(ViewCubeFace::Front);
    };
    entries.push_back(frontEntry);

    ContextMenuEntry backEntry;
    backEntry.label = "Back";
    backEntry.action = [this]() {
        snapCameraToView(ViewCubeFace::Back);
    };
    entries.push_back(backEntry);

    ContextMenuEntry leftEntry;
    leftEntry.label = "Left";
    leftEntry.action = [this]() {
        snapCameraToView(ViewCubeFace::Left);
    };
    entries.push_back(leftEntry);

    ContextMenuEntry rightEntry;
    rightEntry.label = "Right";
    rightEntry.action = [this]() {
        snapCameraToView(ViewCubeFace::Right);
    };
    entries.push_back(rightEntry);

    ContextMenuEntry topEntry;
    topEntry.label = "Top";
    topEntry.action = [this]() {
        snapCameraToView(ViewCubeFace::Top);
    };
    entries.push_back(topEntry);

    ContextMenuEntry bottomEntry;
    bottomEntry.label = "Bottom";
    bottomEntry.action = [this]() {
        snapCameraToView(ViewCubeFace::Bottom);
    };
    entries.push_back(bottomEntry);

    ContextMenuEntry isoEntry;
    isoEntry.label = "Isometric";
    isoEntry.action = [this]() {
        m_camera.setYaw(45.0f);
        m_camera.setPitch(35.0f);
        m_viewCubeCache.valid = false;
    };
    entries.push_back(isoEntry);

    entries.push_back(ContextMenuEntry::separator());

    // Display Controls
    ContextMenuEntry wireframeEntry;
    wireframeEntry.label = "Wireframe";
    wireframeEntry.action = [this]() {
        m_renderer.settings().wireframe = !m_renderer.settings().wireframe;
    };
    entries.push_back(wireframeEntry);

    ContextMenuEntry gridEntry;
    gridEntry.label = "Show Grid";
    gridEntry.action = [this]() {
        m_renderer.settings().showGrid = !m_renderer.settings().showGrid;
    };
    entries.push_back(gridEntry);

    ContextMenuEntry axisEntry;
    axisEntry.label = "Show Axes";
    axisEntry.action = [this]() {
        m_renderer.settings().showAxis = !m_renderer.settings().showAxis;
    };
    entries.push_back(axisEntry);

    ContextMenuEntry envelopeEntry;
    envelopeEntry.label = "Show Work Envelope";
    envelopeEntry.action = []() {
        auto& cfg = Config::instance();
        cfg.setCncShowWorkEnvelope(!cfg.getCncShowWorkEnvelope());
    };
    entries.push_back(envelopeEntry);

    ContextMenuEntry modelBoundsEntry;
    modelBoundsEntry.label = "Show Model Bounds";
    modelBoundsEntry.action = []() {
        auto& cfg = Config::instance();
        cfg.setCncShowModelBounds(!cfg.getCncShowModelBounds());
    };
    entries.push_back(modelBoundsEntry);

    entries.push_back(ContextMenuEntry::separator());

    // Mesh repair
    ContextMenuEntry recalcNormalsEntry;
    recalcNormalsEntry.label = "Recalculate Normals";
    recalcNormalsEntry.action = [this]() {
        recalculateModelNormals();
    };
    recalcNormalsEntry.enabled = [this]() {
        return hasValidModel();
    };
    entries.push_back(recalcNormalsEntry);

    entries.push_back(ContextMenuEntry::separator());

    // Lighting
    ContextMenuEntry lightingEntry;
    lightingEntry.label = "Reset Lighting";
    lightingEntry.action = [this]() {
        auto& rs = m_renderer.settings();
        rs.lightDir = Vec3{-0.5f, -1.0f, -0.3f};
        rs.lightColor = Vec3{1.0f, 1.0f, 1.0f};

        // Persist reset light settings
        auto& cfg = Config::instance();
        cfg.setRenderLightDir(rs.lightDir);
        cfg.setRenderLightColor(rs.lightColor);
        cfg.save();
    };
    entries.push_back(lightingEntry);

    m_contextMenuManager->registerEntries("ViewportPanel_Context", entries);
}

void ViewportPanel::handleInput() {
    if (!ImGui::IsWindowHovered()) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    auto& cfg = Config::instance();
    NavStyle nav = cfg.getNavStyle();
    f32 orbitSignX = cfg.getInvertOrbitX() ? 1.0f : -1.0f;
    f32 orbitSignY = cfg.getInvertOrbitY() ? 1.0f : -1.0f;

    // Mouse sensitivity tuning values (candidates for Config promotion)
    constexpr f32 kWheelZoomSensitivity = 0.5f;            // camera units per wheel tick
    constexpr f32 kDragZoomSensitivity = 0.01f;            // camera units per pixel of Y drag
    constexpr f32 kLightIntensityDragSensitivity = 0.005f; // intensity units per pixel of Y drag

    // Mouse wheel zoom (all styles)
    if (io.MouseWheel != 0.0f) {
        m_camera.zoom(io.MouseWheel * kWheelZoomSensitivity);
    }

    // --- Configurable light controls ---
    InputBinding lightDirBind = cfg.getBinding(BindAction::LightDirDrag);
    InputBinding lightIntBind = cfg.getBinding(BindAction::LightIntensityDrag);

    constexpr f32 kPi = 3.14159265f;
    constexpr f32 kDeg2Rad = kPi / 180.0f;
    constexpr f32 kRad2Deg = 180.0f / kPi;
    constexpr f32 kDragSensitivity = 0.5f; // degrees per pixel

    // Light direction drag
    if (lightDirBind.isValid() && lightDirBind.isHeld()) {
        if (!m_lightDirDragging) {
            m_lightDirDragging = true;
        }
        ImVec2 delta = io.MouseDelta;
        if (delta.x != 0.0f || delta.y != 0.0f) {
            auto& dir = m_renderer.settings().lightDir;
            Vec2 spherical = toSpherical(dir);
            f32 azimuth = spherical.x * kRad2Deg;
            f32 elevation = spherical.y * kRad2Deg;

            azimuth += delta.x * kDragSensitivity;
            elevation += delta.y * kDragSensitivity;
            elevation = std::clamp(elevation, -89.0f, 89.0f);

            dir = fromSpherical(azimuth * kDeg2Rad, elevation * kDeg2Rad);
        }
        return; // consume input
    }
    if (m_lightDirDragging) {
        m_lightDirDragging = false;
        cfg.setRenderLightDir(m_renderer.settings().lightDir);
        cfg.save();
    }

    // Light intensity drag
    if (lightIntBind.isValid() && lightIntBind.isHeld()) {
        if (!m_lightIntensityDragging) {
            m_lightIntensityDragging = true;
        }
        ImVec2 delta = io.MouseDelta;
        if (delta.y != 0.0f) {
            auto& col = m_renderer.settings().lightColor;
            f32 maxC = std::max({col.x, col.y, col.z, 0.001f});
            f32 intensity = maxC - delta.y * kLightIntensityDragSensitivity;
            intensity = std::clamp(intensity, 0.1f, 3.0f);
            f32 scale = intensity / maxC;
            col.x *= scale;
            col.y *= scale;
            col.z *= scale;
        }
        return; // consume input
    }
    if (m_lightIntensityDragging) {
        m_lightIntensityDragging = false;
        cfg.setRenderLightColor(m_renderer.settings().lightColor);
        cfg.save();
    }

    switch (nav) {
    case NavStyle::CAD:
        // Middle=Orbit, Shift+Middle=Pan, Right=Pan, Scroll=Zoom
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImVec2 delta = io.MouseDelta;
            if (io.KeyShift) {
                m_camera.pan(-delta.x, delta.y);
            } else {
                m_camera.orbit(orbitSignX * delta.x, orbitSignY * delta.y);
            }
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            ImVec2 delta = io.MouseDelta;
            m_camera.pan(-delta.x, delta.y);
        }
        break;

    case NavStyle::Maya:
        // Alt+Left=Orbit, Alt+Middle=Pan, Alt+Right=Zoom
        if (io.KeyAlt) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta = io.MouseDelta;
                m_camera.orbit(orbitSignX * delta.x, orbitSignY * delta.y);
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                ImVec2 delta = io.MouseDelta;
                m_camera.pan(-delta.x, delta.y);
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                ImVec2 delta = io.MouseDelta;
                m_camera.zoom(delta.y * kDragZoomSensitivity);
            }
        }
        break;

    default: // NavStyle::Default
        // Left=Orbit, Shift+Left=Pan, Middle=Pan, Right=Zoom
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 delta = io.MouseDelta;
            if (io.KeyShift) {
                m_camera.pan(-delta.x, delta.y);
            } else {
                m_camera.orbit(orbitSignX * delta.x, orbitSignY * delta.y);
            }
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImVec2 delta = io.MouseDelta;
            m_camera.pan(-delta.x, delta.y);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            ImVec2 delta = io.MouseDelta;
            m_camera.zoom(delta.y * 0.01f);
        }
        break;
    }
}

void ViewportPanel::renderViewport() {
    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    int width = static_cast<int>(contentSize.x);
    int height = static_cast<int>(contentSize.y);

    if (width <= 0 || height <= 0) {
        return;
    }

    // Resize framebuffer if needed
    if (width != m_viewportWidth || height != m_viewportHeight) {
        m_viewportWidth = width;
        m_viewportHeight = height;
        m_framebuffer.resize(width, height);
        m_camera.setViewport(width, height);
    }

    // Run alignment validation when dirty (once, not every frame)
    if (m_alignmentDirty && m_hasFitParams && m_mesh && hasGCode()) {
        m_alignmentDirty = false;
        carve::ModelFitter fitter;
        fitter.setModelBounds(m_fitBoundsMin, m_fitBoundsMax);
        fitter.setStock(m_fitStock);
        auto result = carve::validateAlignment(
            m_gcodeProgram, *m_mesh, fitter, m_fitParams);
        if (result.valid) {
            m_alignmentStatus = result.aligned
                ? AlignmentStatus::Aligned
                : AlignmentStatus::Misaligned;
        } else {
            m_alignmentStatus = AlignmentStatus::Unknown;
        }
    }

    // Render to framebuffer
    m_framebuffer.bind();

    m_renderer.beginFrame(Color{0.15f, 0.16f, 0.17f, 1.0f});
    m_renderer.setCamera(m_camera);

    // Render grid and axis (if enabled in settings)
    m_renderer.renderGrid(20.0f, 1.0f);
    m_renderer.renderAxis(2.0f);

    // Render mesh (with material texture if assigned)
    if (m_showModel && m_gpuMesh.vao != 0) {
        m_renderer.renderMesh(m_gpuMesh, m_materialTexture, m_modelMatrix);
    }

    // Render toolpath (if present and visible)
    if (m_showToolpath && m_gpuToolpath.vao != 0) {
        m_renderer.renderToolpath(*m_toolpathMesh);
    }

    // Render G-code line geometry (if present and visible)
    if (m_showGCode) {
        if (m_gcodeDirty) {
            buildGCodeGeometry();
        }
        renderGCodeLines();
    }

    // CNC overlays (work envelope and model bounds)
    auto& cfg = Config::instance();
    const auto& profile = cfg.getActiveMachineProfile();

    // Expand far plane to encompass overlays (may exceed model-fitted frustum)
    f32 savedFar = m_camera.farPlane();
    bool needsExtendedFar = cfg.getCncShowWorkEnvelope() || cfg.getCncShowModelBounds();
    if (needsExtendedFar) {
        f32 envExtent = std::max({profile.maxTravelX, profile.maxTravelY, profile.maxTravelZ});
        f32 needed = (m_camera.distance() + envExtent) * 2.0f;
        if (needed > savedFar) {
            m_camera.setFarPlane(needed);
            m_renderer.setCamera(m_camera);
        }
    }

    // Work envelope (blue box showing machine limits)
    // G-code (X,Y,Z) -> renderer (X,Z,Y): swap Y/Z travel limits
    if (cfg.getCncShowWorkEnvelope()) {
        Vec3 envMax = gcodeToRenderer(Vec3{profile.maxTravelX, profile.maxTravelY, profile.maxTravelZ});
        m_renderer.renderWireBox(Vec3{0, 0, 0}, envMax, cfg.getCncEnvelopeColor());
    }

    // Model bounds overlay — shows model size relative to machine envelope
    if (cfg.getCncShowModelBounds() && m_mesh && m_mesh->isValid()) {
        const AABB& rawBounds = m_mesh->bounds();

        // Transform bounds through model matrix (same as mesh rendering)
        Vec3 transformedMin, transformedMax;
        if (m_hasFitParams) {
            // Apply fit transformation to bounds corners to get accurate AABB
            Vec3 corners[8] = {
                {rawBounds.min.x, rawBounds.min.y, rawBounds.min.z},
                {rawBounds.max.x, rawBounds.min.y, rawBounds.min.z},
                {rawBounds.min.x, rawBounds.max.y, rawBounds.min.z},
                {rawBounds.max.x, rawBounds.max.y, rawBounds.min.z},
                {rawBounds.min.x, rawBounds.min.y, rawBounds.max.z},
                {rawBounds.max.x, rawBounds.min.y, rawBounds.max.z},
                {rawBounds.min.x, rawBounds.max.y, rawBounds.max.z},
                {rawBounds.max.x, rawBounds.max.y, rawBounds.max.z}
            };

            transformedMin = Vec3(FLT_MAX);
            transformedMax = Vec3(-FLT_MAX);

            for (int i = 0; i < 8; i++) {
                Vec4 corner4 = m_modelMatrix * Vec4(corners[i].x, corners[i].y, corners[i].z, 1.0f);
                Vec3 transformedCorner = Vec3(corner4.x, corner4.y, corner4.z);

                transformedMin.x = std::min(transformedMin.x, transformedCorner.x);
                transformedMin.y = std::min(transformedMin.y, transformedCorner.y);
                transformedMin.z = std::min(transformedMin.z, transformedCorner.z);
                transformedMax.x = std::max(transformedMax.x, transformedCorner.x);
                transformedMax.y = std::max(transformedMax.y, transformedCorner.y);
                transformedMax.z = std::max(transformedMax.z, transformedCorner.z);
            }
        } else {
            // No fit params, use mesh bounds as-is (already auto-oriented)
            transformedMin = rawBounds.min;
            transformedMax = rawBounds.max;
        }

        // Check if model fits within machine envelope
        // Transformed bounds are in renderer space (Y-up), so swap profile limits:
        // renderer Y = G-code Z (height), renderer Z = G-code Y (depth)
        bool fitsX = transformedMax.x <= profile.maxTravelX;
        bool fitsY = transformedMax.y <= profile.maxTravelZ;
        bool fitsZ = transformedMax.z <= profile.maxTravelY;
        bool modelFits = fitsX && fitsY && fitsZ &&
                       transformedMin.x >= 0.0f &&
                       transformedMin.y >= 0.0f &&
                       transformedMin.z >= 0.0f;

        // Choose color: Green if model fits within bounds, Red if it exceeds
        Vec4 color = modelFits ? cfg.getCncModelBoundsInColor() : cfg.getCncModelBoundsOutColor();

        // Render transformed model bounds box
        m_renderer.renderWireBox(transformedMin, transformedMax, color);
    }

    // Live CNC tool position (only when connected)
    if (m_cncConnected) {
        // G-code Z (height) -> renderer Y, G-code Y -> renderer Z
        Vec3 renderPos = gcodeToRenderer(m_machineStatus.workPos);

        if (cfg.getCncShowToolDot()) {
            m_renderer.renderPoint(renderPos, cfg.getCncToolDotSize(), cfg.getCncToolDotColor());
        }
    }

    // Restore original far plane
    if (m_camera.farPlane() != savedFar) {
        m_camera.setFarPlane(savedFar);
        m_renderer.setCamera(m_camera);
    }

    m_renderer.endFrame();
    m_framebuffer.unbind();

    // Display framebuffer texture
    ImGui::Image(static_cast<ImTextureID>(m_framebuffer.colorTexture()),
                 contentSize,
                 ImVec2(0, 1),
                 ImVec2(1, 0));

    // Live DRO overlay
    if (m_senderWorkspaceActive && m_cncConnected && Config::instance().getCncShowDroOverlay()) {
        renderCncDro();
    }

    // Handle input after Image so ImGui's item/window hover state is fully resolved.
    // This prevents the viewport from stealing clicks when a floating panel overlaps it.
    handleInput();

    // Context menu for viewport interactions
    if (m_contextMenuManager != nullptr) {
        if (ImGui::BeginPopupContextItem("ViewportPanel_Context")) {
            m_contextMenuManager->render("ViewportPanel_Context");
            ImGui::EndPopup();
        }
    }

    renderViewCube();
}

void ViewportPanel::renderToolbar() {
    auto& style = ImGui::GetStyle();
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(style.ItemSpacing.y, style.ItemSpacing.y));
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(style.FramePadding.x, style.FramePadding.y));

    if (ImGui::Button(Icons::Refresh)) {
        resetView();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset View");
    ImGui::SameLine();

    if (ImGui::Button(Icons::Fit)) {
        fitToModel();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fit to Model");
    ImGui::SameLine();

    ImGui::Separator();
    ImGui::SameLine();

    // Wrap Wireframe checkbox to new line if too narrow
    if (ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 6.0f) {
        ImGui::NewLine();
    }

    bool wireframe = m_renderer.settings().wireframe;
    if (ImGui::Checkbox("Wireframe", &wireframe)) {
        m_renderer.settings().wireframe = wireframe;
    }

    // Master visibility toggles (always visible)
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    ImGui::Checkbox("Model", &m_showModel);
    if (m_gpuToolpath.vao != 0) {
        ImGui::SameLine();
        ImGui::Checkbox("Toolpath", &m_showToolpath);
    }
    if (hasGCode()) {
        ImGui::SameLine();
        if (ImGui::Checkbox("G-code", &m_showGCode))
            m_gcodeDirty = true;
    }

    // Alignment status indicator (only when FitParams are active)
    if (m_hasFitParams && m_alignmentStatus != AlignmentStatus::Unknown) {
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        if (m_alignmentStatus == AlignmentStatus::Aligned) {
            ImGui::PushStyleColor(ImGuiCol_Text, colors::kSuccess);
            ImGui::Text("Aligned");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, colors::kError);
            ImGui::Text("Misaligned");
            ImGui::PopStyleColor();
        }
    }

    // G-code move-type toggles (only when G-code loaded + G-code visible)
    if (hasGCode() && m_showGCode) {
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        if (ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 22.0f) {
            ImGui::NewLine();
        }

        if (ImGui::Checkbox("Rapid", &m_showRapids))
            m_gcodeDirty = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Cut", &m_showCuts))
            m_gcodeDirty = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Plunge", &m_showPlunges))
            m_gcodeDirty = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Retract", &m_showRetracts))
            m_gcodeDirty = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Color by Tool", &m_colorByTool))
            m_gcodeDirty = true;

        // Z-clip slider
        ImGui::Text("Z clip:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(
            ImGui::GetContentRegionAvail().x);
        if (ImGui::SliderFloat(
                "##VPZClip", &m_zClipMax,
                m_gcodeProgram.boundsMin.z,
                m_zClipMaxBound, "%.2f mm")) {
            m_gcodeDirty = true;
        }

        // Simulation controls
        renderSimControls();
    }

    ImGui::PopStyleVar(2);
}

void ViewportPanel::renderViewCube() {
    constexpr f32 kCubeSize = 30.0f;
    constexpr f32 kMargin = 15.0f;
    constexpr f32 kDeg2Rad = 3.14159265f / 180.0f;
    constexpr f32 kEpsilon = 0.001f;

    // Cube vertices: unit cube from -1 to 1
    const std::array<Vec3, 8> verts = {{
        {-1, -1, -1},
        {1, -1, -1},
        {1, 1, -1},
        {-1, 1, -1},
        {-1, -1, 1},
        {1, -1, 1},
        {1, 1, 1},
        {-1, 1, 1},
    }};

    // Face definitions: 4 vertex indices, label, snap face
    struct Face {
        int v[4];
        const char* label;
        ViewCubeFace snapFace;
    };

    const std::array<Face, 6> faces = {{
        {{0, 1, 2, 3}, "Bk", ViewCubeFace::Back},   // Back (-Z)
        {{5, 4, 7, 6}, "F", ViewCubeFace::Front},   // Front (+Z)
        {{1, 5, 6, 2}, "R", ViewCubeFace::Right},   // Right (+X)
        {{4, 0, 3, 7}, "L", ViewCubeFace::Left},    // Left (-X)
        {{3, 2, 6, 7}, "T", ViewCubeFace::Top},     // Top (+Y)
        {{4, 5, 1, 0}, "Bt", ViewCubeFace::Bottom}, // Bottom (-Y)
    }};

    // Face colors (RGBA)
    constexpr ImU32 kFaceColor = IM_COL32(70, 75, 85, 220);
    constexpr ImU32 kFaceHover = IM_COL32(100, 110, 130, 240);
    constexpr ImU32 kEdgeColor = IM_COL32(40, 42, 48, 255);
    constexpr ImU32 kLabelColor = IM_COL32(200, 210, 225, 255);
    constexpr ImU32 kFrontFace = IM_COL32(85, 95, 110, 230);

    // Get viewport image rect (from previous ImGui::Image call)
    ImVec2 rectMin = ImGui::GetItemRectMin();
    ImVec2 rectMax = ImGui::GetItemRectMax();

    // Cube origin: top-right corner
    ImVec2 origin = {rectMax.x - kMargin - kCubeSize, rectMin.y + kMargin + kCubeSize};

    // --- Cache check: skip geometry recomputation if camera unchanged ---
    f32 yaw = m_camera.yaw();
    f32 pitch = m_camera.pitch();

    if (!m_viewCubeCache.valid || std::abs(yaw - m_viewCubeCache.lastYaw) > kEpsilon ||
        std::abs(pitch - m_viewCubeCache.lastPitch) > kEpsilon) {

        // Recompute geometry: build rotation matrix from camera yaw/pitch
        f32 yawRad = -yaw * kDeg2Rad;
        f32 pitchRad = pitch * kDeg2Rad;

        f32 cy = std::cos(yawRad), sy = std::sin(yawRad);
        f32 cp = std::cos(pitchRad), sp = std::sin(pitchRad);

        // Rotation: yaw around Y, then pitch around X
        // R = Rx(pitch) * Ry(yaw)
        auto rotate = [&](const Vec3& v) -> Vec3 {
            // Ry
            f32 rx = cy * v.x + sy * v.z;
            f32 ry = v.y;
            f32 rz = -sy * v.x + cy * v.z;
            // Rx
            f32 fx = rx;
            f32 fy = cp * ry - sp * rz;
            f32 fz = sp * ry + cp * rz;
            return {fx, fy, fz};
        };

        // Rotate all vertices and store as OFFSETS (relative to origin)
        for (int i = 0; i < 8; i++) {
            Vec3 r = rotate(verts[static_cast<usize>(i)]);
            // Store offsets, not absolute coords
            m_viewCubeCache.projectedVerts[static_cast<usize>(i)] = {r.x * kCubeSize,
                                                                     -r.y * kCubeSize};
            m_viewCubeCache.depths[static_cast<usize>(i)] = r.z;
        }

        // Sort faces back-to-front by average Z
        for (int i = 0; i < 6; i++) {
            const auto& f = faces[static_cast<usize>(i)];
            f32 avgZ = (m_viewCubeCache.depths[static_cast<usize>(f.v[0])] +
                        m_viewCubeCache.depths[static_cast<usize>(f.v[1])] +
                        m_viewCubeCache.depths[static_cast<usize>(f.v[2])] +
                        m_viewCubeCache.depths[static_cast<usize>(f.v[3])]) *
                       0.25f;
            m_viewCubeCache.sortedFaces[static_cast<usize>(i)] = {i, avgZ};
        }
        std::sort(m_viewCubeCache.sortedFaces.begin(),
                  m_viewCubeCache.sortedFaces.end(),
                  [](const ViewCubeCache::FaceSort& a, const ViewCubeCache::FaceSort& b) {
                      return a.avgZ < b.avgZ;
                  });

        m_viewCubeCache.lastYaw = yaw;
        m_viewCubeCache.lastPitch = pitch;
        m_viewCubeCache.valid = true;
    }

    // Apply origin offset to cached verts for drawing (convert offsets to absolute coords)
    std::array<ImVec2, 8> proj;
    for (int i = 0; i < 8; i++) {
        proj[static_cast<usize>(i)] = {
            origin.x + m_viewCubeCache.projectedVerts[static_cast<usize>(i)].x,
            origin.y + m_viewCubeCache.projectedVerts[static_cast<usize>(i)].y};
    }

    // Hit test: check mouse against faces front-to-back
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    int hoveredFace = -1;
    int clickedFace = -1;

    // Point-in-quad test using cross products
    auto pointInQuad = [](ImVec2 p, ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d) -> bool {
        auto cross2d = [](ImVec2 o, ImVec2 e, ImVec2 pt) -> f32 {
            return (e.x - o.x) * (pt.y - o.y) - (e.y - o.y) * (pt.x - o.x);
        };
        f32 c0 = cross2d(a, b, p);
        f32 c1 = cross2d(b, c, p);
        f32 c2 = cross2d(c, d, p);
        f32 c3 = cross2d(d, a, p);
        return (c0 >= 0 && c1 >= 0 && c2 >= 0 && c3 >= 0) ||
               (c0 <= 0 && c1 <= 0 && c2 <= 0 && c3 <= 0);
    };

    // Check front-to-back (reverse of cached sorted order)
    for (int si = 5; si >= 0; si--) {
        const auto& fs = m_viewCubeCache.sortedFaces[static_cast<usize>(si)];
        const auto& f = faces[static_cast<usize>(fs.index)];
        if (fs.avgZ < 0)
            continue; // Skip back faces for hit testing

        ImVec2 qa = proj[static_cast<usize>(f.v[0])];
        ImVec2 qb = proj[static_cast<usize>(f.v[1])];
        ImVec2 qc = proj[static_cast<usize>(f.v[2])];
        ImVec2 qd = proj[static_cast<usize>(f.v[3])];

        if (pointInQuad(mousePos, qa, qb, qc, qd)) {
            hoveredFace = fs.index;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                clickedFace = fs.index;
            }
            break;
        }
    }

    // Draw faces back-to-front
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    for (int si = 0; si < 6; si++) {
        const auto& fs = m_viewCubeCache.sortedFaces[static_cast<usize>(si)];
        const auto& f = faces[static_cast<usize>(fs.index)];

        ImVec2 qa = proj[static_cast<usize>(f.v[0])];
        ImVec2 qb = proj[static_cast<usize>(f.v[1])];
        ImVec2 qc = proj[static_cast<usize>(f.v[2])];
        ImVec2 qd = proj[static_cast<usize>(f.v[3])];

        // Choose color based on facing and hover
        ImU32 color = kFaceColor;
        if (fs.avgZ > 0.3f)
            color = kFrontFace;
        if (fs.index == hoveredFace)
            color = kFaceHover;

        drawList->AddQuadFilled(qa, qb, qc, qd, color);
        drawList->AddQuad(qa, qb, qc, qd, kEdgeColor, 1.0f);

        // Draw label on front-facing faces
        if (fs.avgZ > 0.0f) {
            ImVec2 center = {
                (qa.x + qb.x + qc.x + qd.x) * 0.25f,
                (qa.y + qb.y + qc.y + qd.y) * 0.25f,
            };
            ImVec2 textSize = ImGui::CalcTextSize(f.label);
            drawList->AddText({center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f},
                              kLabelColor,
                              f.label);
        }
    }

    // Handle click: snap camera to target orientation
    if (clickedFace >= 0) {
        const auto& f = faces[static_cast<usize>(clickedFace)];
        snapCameraToView(f.snapFace);
    }
}

void ViewportPanel::snapCameraToView(ViewCubeFace face) {
    auto snap = snapViewCubeOrientation(face, m_camera.yaw());
    m_camera.setYaw(snap.yawDeg);
    m_camera.setPitch(snap.pitchDeg);
    m_viewCubeCache.valid = false;
}

// --- G-code line rendering ---

void ViewportPanel::setGCodeProgram(const gcode::Program& program) {
    m_gcodeProgram = program;
    m_gcodeDirty = true;
    m_alignmentDirty = true;

    // Initialize Z-clip bounds from program
    m_zClipMaxBound = program.boundsMax.z;
    m_zClipMax = m_zClipMaxBound;

    // Fit camera to G-code bounds if no mesh is currently loaded
    if (!m_mesh) {
        // Swap Y<->Z: G-code uses Z-up, renderer uses Y-up
        Vec3 bMin = gcodeToRenderer(program.boundsMin);
        Vec3 bMax = gcodeToRenderer(program.boundsMax);
        m_camera.fitToBounds(bMin, bMax);
    }

    // Invalidate ViewCube cache
    m_viewCubeCache.valid = false;
}

void ViewportPanel::setGCodeStatistics(const gcode::Statistics& stats) {
    // Precompute segment times from Statistics (convert minutes -> seconds)
    m_segmentTimes.resize(stats.segmentTimes.size());
    for (size_t i = 0; i < stats.segmentTimes.size(); ++i)
        m_segmentTimes[i] = stats.segmentTimes[i] * 60.0f;

    // Build cumulative sum for O(log n) binary search during scrubbing
    m_segmentTimeCumulative.resize(m_segmentTimes.size());
    if (!m_segmentTimes.empty()) {
        std::partial_sum(m_segmentTimes.begin(), m_segmentTimes.end(),
                         m_segmentTimeCumulative.begin());
        m_simTotalTime = m_segmentTimeCumulative.back();
    } else {
        m_simTotalTime = 0.0f;
    }

    // Reset simulation state
    m_simState = VPSimState::Stopped;
    m_simTime = 0.0f;
    m_simSegmentIndex = 0;
    m_simSegmentProgress = 0.0f;
}

void ViewportPanel::clearGCodeProgram() {
    m_gcodeProgram = gcode::Program{};
    m_zClipMax = 100.0f;
    m_zClipMaxBound = 100.0f;
    m_gcToolGroups.clear();
    destroyGCodeGeometry();
    m_alignmentStatus = AlignmentStatus::Unknown;

    // Reset simulation state
    m_simState = VPSimState::Stopped;
    m_simTime = 0.0f;
    m_simSegmentIndex = 0;
    m_simSegmentProgress = 0.0f;
    m_segmentTimes.clear();
    m_segmentTimeCumulative.clear();
    destroySimGeometry();
}

void ViewportPanel::destroyGCodeGeometry() {
    if (m_gcodeVBO != 0) {
        glDeleteBuffers(1, &m_gcodeVBO);
        m_gcodeVBO = 0;
    }
    if (m_gcodeVAO != 0) {
        glDeleteVertexArrays(1, &m_gcodeVAO);
        m_gcodeVAO = 0;
    }
    m_gcRapidStart = m_gcRapidCount = 0;
    m_gcCutStart = m_gcCutCount = 0;
    m_gcPlungeStart = m_gcPlungeCount = 0;
    m_gcRetractStart = m_gcRetractCount = 0;
    destroySimGeometry();
}

void ViewportPanel::destroySimGeometry() {
    if (m_simVBO != 0) {
        glDeleteBuffers(1, &m_simVBO);
        m_simVBO = 0;
    }
    if (m_simVAO != 0) {
        glDeleteVertexArrays(1, &m_simVAO);
        m_simVAO = 0;
    }
}

void ViewportPanel::updateSimulation(float dt) {
    if (m_simState != VPSimState::Playing || m_gcodeProgram.path.empty())
        return;

    m_simTime += dt * m_simSpeed;

    if (m_segmentTimeCumulative.empty()) {
        m_simState = VPSimState::Stopped;
        return;
    }

    if (m_simTime >= m_simTotalTime) {
        // Past end -- stop
        m_simSegmentIndex = m_gcodeProgram.path.size();
        m_simSegmentProgress = 0.0f;
        m_simState = VPSimState::Stopped;
        return;
    }

    // O(log n) binary search on cumulative time array
    auto it = std::lower_bound(m_segmentTimeCumulative.begin(),
                                m_segmentTimeCumulative.end(), m_simTime);
    size_t idx = static_cast<size_t>(it - m_segmentTimeCumulative.begin());
    if (idx >= m_gcodeProgram.path.size())
        idx = m_gcodeProgram.path.size() - 1;

    float segStart = (idx > 0) ? m_segmentTimeCumulative[idx - 1] : 0.0f;
    float segDur = m_segmentTimes[idx];
    m_simSegmentIndex = idx;
    m_simSegmentProgress = (segDur > 0.0f) ? (m_simTime - segStart) / segDur : 0.0f;
}

void ViewportPanel::buildGCodeGeometry() {
    destroyGCodeGeometry();
    m_gcToolGroups.clear();

    if (m_gcodeProgram.path.empty()) {
        m_gcodeDirty = false;
        return;
    }

    // Lambda to push a segment's vertices with Y<->Z swap
    auto addSegVerts = [](std::vector<f32>& verts,
                          const gcode::PathSegment& seg) {
        verts.push_back(seg.start.x);
        verts.push_back(seg.start.z); // G-code Z -> renderer Y
        verts.push_back(seg.start.y); // G-code Y -> renderer Z
        verts.push_back(seg.end.x);
        verts.push_back(seg.end.z);
        verts.push_back(seg.end.y);
    };

    // Helper: classify non-rapid segment visibility
    auto isVisibleNonRapid =
        [this](const gcode::PathSegment& seg) -> bool {
        float dz = seg.end.z - seg.start.z;
        float dxy2 =
            (seg.end.x - seg.start.x) * (seg.end.x - seg.start.x) +
            (seg.end.y - seg.start.y) * (seg.end.y - seg.start.y);
        bool zDominant = (dz * dz) > dxy2 * 0.25f;

        if (dz < -0.001f && zDominant) return m_showPlunges;
        if (dz > 0.001f && zDominant) return m_showRetracts;
        return m_showCuts;
    };

    std::vector<f32> allVerts;
    const std::size_t segmentCount = m_gcodeProgram.path.size();
    const std::size_t viewportStride =
        gcode::viewportSegmentStride(segmentCount);

    if (m_colorByTool) {
        // Color-by-tool mode: group by tool number
        std::vector<f32> rapidVerts;
        std::map<int, std::vector<f32>> toolVerts;

        for (std::size_t i = 0; i < segmentCount; ++i) {
            if (!gcode::shouldIncludeViewportSegment(i, segmentCount, viewportStride)) {
                continue;
            }
            const auto& seg = m_gcodeProgram.path[i];
            if (seg.end.z > m_zClipMax) continue;

            if (seg.isRapid) {
                if (m_showRapids) addSegVerts(rapidVerts, seg);
            } else {
                if (!isVisibleNonRapid(seg)) continue;
                addSegVerts(toolVerts[seg.toolNumber], seg);
            }
        }

        allVerts.reserve(rapidVerts.size());
        for (auto& [tool, verts] : toolVerts)
            allVerts.reserve(allVerts.capacity() + verts.size());

        // Rapids first
        m_gcRapidStart = 0;
        m_gcRapidCount =
            static_cast<u32>(rapidVerts.size() / 3);
        allVerts.insert(
            allVerts.end(), rapidVerts.begin(), rapidVerts.end());

        // Zero out type-based groups (not used in tool mode)
        m_gcCutStart = m_gcCutCount = 0;
        m_gcPlungeStart = m_gcPlungeCount = 0;
        m_gcRetractStart = m_gcRetractCount = 0;

        // Tool groups
        u32 offset = m_gcRapidCount;
        for (auto& [tool, verts] : toolVerts) {
            ToolGroup tg;
            tg.toolNumber = tool;
            tg.start = offset;
            tg.count = static_cast<u32>(verts.size() / 3);
            m_gcToolGroups.push_back(tg);
            allVerts.insert(
                allVerts.end(), verts.begin(), verts.end());
            offset += tg.count;
        }
    } else {
        // Standard mode: group by move type with filtering
        std::vector<f32> rapidVerts;
        std::vector<f32> cutVerts;
        std::vector<f32> plungeVerts;
        std::vector<f32> retractVerts;

        for (std::size_t i = 0; i < segmentCount; ++i) {
            if (!gcode::shouldIncludeViewportSegment(i, segmentCount, viewportStride)) {
                continue;
            }
            const auto& seg = m_gcodeProgram.path[i];
            if (seg.end.z > m_zClipMax) continue;

            if (seg.isRapid) {
                if (!m_showRapids) continue;
                addSegVerts(rapidVerts, seg);
            } else {
                float dz = seg.end.z - seg.start.z;
                float dxy2 =
                    (seg.end.x - seg.start.x) *
                        (seg.end.x - seg.start.x) +
                    (seg.end.y - seg.start.y) *
                        (seg.end.y - seg.start.y);
                bool zDominant = (dz * dz) > dxy2 * 0.25f;

                if (dz < -0.001f && zDominant) {
                    if (!m_showPlunges) continue;
                    addSegVerts(plungeVerts, seg);
                } else if (dz > 0.001f && zDominant) {
                    if (!m_showRetracts) continue;
                    addSegVerts(retractVerts, seg);
                } else {
                    if (!m_showCuts) continue;
                    addSegVerts(cutVerts, seg);
                }
            }
        }

        allVerts.reserve(
            rapidVerts.size() + cutVerts.size() +
            plungeVerts.size() + retractVerts.size());

        m_gcRapidStart = 0;
        m_gcRapidCount =
            static_cast<u32>(rapidVerts.size() / 3);
        allVerts.insert(
            allVerts.end(), rapidVerts.begin(), rapidVerts.end());

        m_gcCutStart = m_gcRapidCount;
        m_gcCutCount =
            static_cast<u32>(cutVerts.size() / 3);
        allVerts.insert(
            allVerts.end(), cutVerts.begin(), cutVerts.end());

        m_gcPlungeStart = m_gcCutStart + m_gcCutCount;
        m_gcPlungeCount =
            static_cast<u32>(plungeVerts.size() / 3);
        allVerts.insert(
            allVerts.end(), plungeVerts.begin(), plungeVerts.end());

        m_gcRetractStart = m_gcPlungeStart + m_gcPlungeCount;
        m_gcRetractCount =
            static_cast<u32>(retractVerts.size() / 3);
        allVerts.insert(
            allVerts.end(), retractVerts.begin(), retractVerts.end());
    }

    if (allVerts.empty()) {
        m_gcodeDirty = false;
        return;
    }

    // Upload to GPU
    GL_CHECK(glGenVertexArrays(1, &m_gcodeVAO));
    GL_CHECK(glGenBuffers(1, &m_gcodeVBO));

    GL_CHECK(glBindVertexArray(m_gcodeVAO));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_gcodeVBO));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER,
                          static_cast<GLsizeiptr>(
                              allVerts.size() * sizeof(f32)),
                          allVerts.data(),
                          GL_STATIC_DRAW));

    // Position attribute (location 0): vec3
    GL_CHECK(glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(f32), nullptr));
    GL_CHECK(glEnableVertexAttribArray(0));

    GL_CHECK(glBindVertexArray(0));

    m_gcodeDirty = false;
}

void ViewportPanel::renderSimControls() {
    if (!hasGCode() || m_segmentTimes.empty())
        return;

    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Play / Pause toggle
    if (m_simState == VPSimState::Stopped || m_simState == VPSimState::Paused) {
        if (ImGui::Button("Play##Sim")) {
            m_simState = VPSimState::Playing;
        }
    } else {
        if (ImGui::Button("Pause##Sim")) {
            m_simState = VPSimState::Paused;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Stop##Sim")) {
        m_simState = VPSimState::Stopped;
        m_simTime = 0.0f;
        m_simSegmentIndex = 0;
        m_simSegmentProgress = 0.0f;
    }

    // Speed selector
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 3);
    ImGui::Text("Speed:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("000000").x + ImGui::GetStyle().FramePadding.x * 2);
    static const float speeds[] = {0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
    static const char* speedLabels[] = {"0.5x", "1x", "2x", "5x", "10x"};
    int currentSpeedIdx = 1;
    for (int i = 0; i < 5; ++i) {
        if (m_simSpeed == speeds[i])
            currentSpeedIdx = i;
    }
    if (ImGui::BeginCombo("##VPSimSpeed", speedLabels[currentSpeedIdx])) {
        for (int i = 0; i < 5; ++i) {
            if (ImGui::Selectable(speedLabels[i], i == currentSpeedIdx))
                m_simSpeed = speeds[i];
        }
        ImGui::EndCombo();
    }

    // Scrub slider
    ImGui::Text("Progress:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    float scrubFrac = (m_simTotalTime > 0.0f) ? (m_simTime / m_simTotalTime) : 0.0f;
    if (ImGui::SliderFloat("##VPSimScrub", &scrubFrac, 0.0f, 1.0f, "%.1f%%")) {
        m_simTime = scrubFrac * m_simTotalTime;
        // O(log n) binary search to find segment from time
        if (!m_segmentTimeCumulative.empty()) {
            auto it = std::lower_bound(m_segmentTimeCumulative.begin(),
                                        m_segmentTimeCumulative.end(), m_simTime);
            size_t idx = static_cast<size_t>(it - m_segmentTimeCumulative.begin());
            if (idx >= m_gcodeProgram.path.size())
                idx = m_gcodeProgram.path.size() - 1;
            float segStart = (idx > 0) ? m_segmentTimeCumulative[idx - 1] : 0.0f;
            float segDur = m_segmentTimes[idx];
            m_simSegmentIndex = idx;
            m_simSegmentProgress = (segDur > 0.0f) ? (m_simTime - segStart) / segDur : 0.0f;
        } else {
            m_simSegmentIndex = 0;
            m_simSegmentProgress = 0.0f;
        }
        // When scrubbing, ensure sim is in a visible state
        if (m_simState == VPSimState::Stopped)
            m_simState = VPSimState::Paused;
    }
}

void ViewportPanel::renderGCodeLines() {
    if (m_gcodeVAO == 0) {
        return;
    }

    Shader& flat = m_renderer.flatShader();
    Shader& heightShader = m_renderer.heightLineShader();
    Mat4 mvp = m_camera.viewProjectionMatrix();

    // Y bounds in renderer space (G-code Z -> renderer Y)
    float yMin = m_gcodeProgram.boundsMin.z;
    float yMax = m_gcodeProgram.boundsMax.z;

    bool simActive = m_simState != VPSimState::Stopped;

    glDisable(GL_CULL_FACE);
    glLineWidth(1.5f);
    glBindVertexArray(m_gcodeVAO);

    if (simActive) {
        // During simulation: draw ALL base geometry as dim ghost lines
        u32 totalVerts = m_gcRapidCount + m_gcCutCount +
                         m_gcPlungeCount + m_gcRetractCount;
        if (!m_gcToolGroups.empty()) {
            for (const auto& tg : m_gcToolGroups)
                totalVerts += tg.count;
            // Subtract the type-based counts since tool groups replace them
            totalVerts = m_gcRapidCount;
            for (const auto& tg : m_gcToolGroups)
                totalVerts += tg.count;
        }
        flat.bind();
        flat.setMat4("uMVP", mvp);
        flat.setVec4("uColor", Vec4{0.3f, 0.3f, 0.35f, 0.35f});
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(totalVerts));
    } else if (m_colorByTool && !m_gcToolGroups.empty()) {
        // Color-by-tool mode: rapids gray, then each tool group
        flat.bind();
        flat.setMat4("uMVP", mvp);
        if (m_gcRapidCount > 0) {
            flat.setVec4("uColor",
                Vec4{0.4f, 0.4f, 0.4f, 0.5f});
            glDrawArrays(
                GL_LINES,
                static_cast<GLint>(m_gcRapidStart),
                static_cast<GLsizei>(m_gcRapidCount));
        }

        // Tool groups use height shader for depth
        heightShader.bind();
        heightShader.setMat4("uMVP", mvp);
        heightShader.setFloat("uYMin", yMin);
        heightShader.setFloat("uYMax", yMax);
        for (const auto& tg : m_gcToolGroups) {
            if (tg.count == 0) continue;
            Vec3 tc = toolColor(tg.toolNumber);
            heightShader.setVec4("uColorLow",
                Vec4{tc.x * 0.3f, tc.y * 0.3f,
                     tc.z * 0.3f, 1.0f});
            heightShader.setVec4("uColorHigh",
                Vec4{tc.x, tc.y, tc.z, 1.0f});
            glDrawArrays(
                GL_LINES,
                static_cast<GLint>(tg.start),
                static_cast<GLsizei>(tg.count));
        }
    } else {
        // Standard mode: 4-group rendering
        flat.bind();
        flat.setMat4("uMVP", mvp);

        // Rapids: dim gray
        if (m_gcRapidCount > 0) {
            flat.setVec4("uColor",
                Vec4{0.4f, 0.4f, 0.4f, 0.5f});
            glDrawArrays(
                GL_LINES,
                static_cast<GLint>(m_gcRapidStart),
                static_cast<GLsizei>(m_gcRapidCount));
        }

        // Cuts: height-colored (deep blue -> bright cyan)
        if (m_gcCutCount > 0) {
            heightShader.bind();
            heightShader.setMat4("uMVP", mvp);
            heightShader.setFloat("uYMin", yMin);
            heightShader.setFloat("uYMax", yMax);
            heightShader.setVec4("uColorLow",
                Vec4{0.05f, 0.15f, 0.5f, 1.0f});
            heightShader.setVec4("uColorHigh",
                Vec4{0.3f, 0.8f, 1.0f, 1.0f});
            glDrawArrays(
                GL_LINES,
                static_cast<GLint>(m_gcCutStart),
                static_cast<GLsizei>(m_gcCutCount));
            // Re-bind flat shader for remaining groups
            flat.bind();
            flat.setMat4("uMVP", mvp);
        }

        // Plunges: orange
        if (m_gcPlungeCount > 0) {
            flat.setVec4("uColor",
                Vec4{1.0f, 0.5f, 0.1f, 1.0f});
            glDrawArrays(
                GL_LINES,
                static_cast<GLint>(m_gcPlungeStart),
                static_cast<GLsizei>(m_gcPlungeCount));
        }

        // Retracts: green
        if (m_gcRetractCount > 0) {
            flat.setVec4("uColor",
                Vec4{0.3f, 0.8f, 0.3f, 0.6f});
            glDrawArrays(
                GL_LINES,
                static_cast<GLint>(m_gcRetractStart),
                static_cast<GLsizei>(m_gcRetractCount));
        }
    }

    // --- Simulation overlay: completed + current segment ---
    if (simActive) {
        flat.bind();
        flat.setMat4("uMVP", mvp);
        std::vector<f32> simVerts;
        simVerts.reserve((m_simSegmentIndex + 1) * 6);

        for (size_t si = 0; si < m_simSegmentIndex && si < m_gcodeProgram.path.size(); ++si) {
            const auto& seg = m_gcodeProgram.path[si];
            if (seg.end.z > m_zClipMax) continue;
            Vec3 rStart = gcodeToRenderer(seg.start);
            Vec3 rEnd   = gcodeToRenderer(seg.end);
            simVerts.push_back(rStart.x);
            simVerts.push_back(rStart.y);
            simVerts.push_back(rStart.z);
            simVerts.push_back(rEnd.x);
            simVerts.push_back(rEnd.y);
            simVerts.push_back(rEnd.z);
        }

        u32 completedVertCount = static_cast<u32>(simVerts.size() / 3);

        // Current segment partial
        if (m_simSegmentIndex < m_gcodeProgram.path.size()) {
            const auto& cur = m_gcodeProgram.path[m_simSegmentIndex];
            float t = std::clamp(m_simSegmentProgress, 0.0f, 1.0f);
            float ex = cur.start.x + (cur.end.x - cur.start.x) * t;
            float ey = cur.start.y + (cur.end.y - cur.start.y) * t;
            float ez = cur.start.z + (cur.end.z - cur.start.z) * t;
            Vec3 rCurStart = gcodeToRenderer(cur.start);
            Vec3 rCurEnd   = gcodeToRenderer(Vec3{ex, ey, ez});
            simVerts.push_back(rCurStart.x);
            simVerts.push_back(rCurStart.y);
            simVerts.push_back(rCurStart.z);
            simVerts.push_back(rCurEnd.x);
            simVerts.push_back(rCurEnd.y);
            simVerts.push_back(rCurEnd.z);
        }

        if (!simVerts.empty()) {
            // Create/update sim VBO
            if (m_simVAO == 0) {
                GL_CHECK(glGenVertexArrays(1, &m_simVAO));
                GL_CHECK(glGenBuffers(1, &m_simVBO));
                GL_CHECK(glBindVertexArray(m_simVAO));
                GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_simVBO));
                GL_CHECK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(f32), nullptr));
                GL_CHECK(glEnableVertexAttribArray(0));
            } else {
                GL_CHECK(glBindVertexArray(m_simVAO));
                GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_simVBO));
            }

            GL_CHECK(glBufferData(GL_ARRAY_BUFFER,
                                  static_cast<GLsizeiptr>(simVerts.size() * sizeof(f32)),
                                  simVerts.data(),
                                  GL_DYNAMIC_DRAW));

            // Draw completed in bright green
            if (completedVertCount > 0) {
                flat.setVec4("uColor", Vec4{0.1f, 0.85f, 0.1f, 1.0f});
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(completedVertCount));
            }

            // Draw current segment in yellow
            if (m_simSegmentIndex < m_gcodeProgram.path.size()) {
                flat.setVec4("uColor", Vec4{1.0f, 0.85f, 0.2f, 1.0f});
                glDrawArrays(GL_LINES, static_cast<GLint>(completedVertCount), 2);

                // Cutter dot at current position
                flat.setVec4("uColor", Vec4{1.0f, 0.2f, 0.2f, 1.0f});
                glPointSize(8.0f);
                glDrawArrays(GL_POINTS, static_cast<GLint>(completedVertCount) + 1, 1);
                glPointSize(1.0f);
            }
        }
    }

    glBindVertexArray(0);
    glLineWidth(1.0f);
    glEnable(GL_CULL_FACE);
}

void ViewportPanel::renderCncDro() {
    ImVec2 rectMin = ImGui::GetItemRectMin();
    ImVec2 rectMax = ImGui::GetItemRectMax();
    ImVec2 rectSize = {rectMax.x - rectMin.x, rectMax.y - rectMin.y};
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const auto& wp = m_machineStatus.workPos;
    Vec3 renderPos = gcodeToRenderer(wp);
    Vec4 clip = m_camera.viewProjectionMatrix() * Vec4(renderPos.x, renderPos.y, renderPos.z, 1.0f);
    if (clip.w <= 0.0f) {
        return;
    }

    Vec3 ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
    if (ndc.z < -1.0f || ndc.z > 1.0f) {
        return;
    }

    ImVec2 anchor{
        rectMin.x + (ndc.x * 0.5f + 0.5f) * rectSize.x,
        rectMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * rectSize.y
    };

    bool metric = Config::instance().getDisplayUnitsMetric();
    const char* unit = metric ? "mm" : "in";
    f32 scale = metric ? 1.0f : (1.0f / 25.4f);

    char xBuf[32], yBuf[32], zBuf[32];
    std::snprintf(xBuf, sizeof(xBuf), "X: %8.3f %s", static_cast<double>(wp.x * scale), unit);
    std::snprintf(yBuf, sizeof(yBuf), "Y: %8.3f %s", static_cast<double>(wp.y * scale), unit);
    std::snprintf(zBuf, sizeof(zBuf), "Z: %8.3f %s", static_cast<double>(wp.z * scale), unit);

    f32 lineH = ImGui::GetTextLineHeightWithSpacing();
    f32 padding = ImGui::GetStyle().FramePadding.x * 1.75f;
    f32 textW = ImGui::CalcTextSize(xBuf).x;
    f32 boxW = textW + padding * 2.0f;
    f32 boxH = lineH * 3.0f + padding * 2.0f;

    ImVec2 boxMin = {anchor.x + padding * 1.5f, anchor.y - boxH - padding * 1.5f};
    boxMin.x = std::clamp(boxMin.x, rectMin.x + padding, rectMax.x - boxW - padding);
    boxMin.y = std::clamp(boxMin.y, rectMin.y + padding, rectMax.y - boxH - padding);
    ImVec2 boxMax = {boxMin.x + boxW, boxMin.y + boxH};

    dl->AddCircleFilled(anchor, 4.0f, IM_COL32(255, 255, 255, 230));
    dl->AddCircle(anchor, 6.0f, IM_COL32(0, 0, 0, 230), 16, 1.5f);
    dl->AddLine(anchor, boxMin, IM_COL32(80, 90, 100, 220), 1.0f);
    dl->AddRectFilled(boxMin, boxMax, IM_COL32(8, 10, 12, 245), 4.0f);
    dl->AddRect(boxMin, boxMax, IM_COL32(80, 90, 100, 220), 4.0f, 0, 1.0f);

    f32 textX = boxMin.x + padding;
    f32 textY = boxMin.y + padding;
    dl->AddText({textX, textY}, IM_COL32(255, 80, 80, 255), xBuf);
    dl->AddText({textX, textY + lineH}, IM_COL32(80, 255, 80, 255), yBuf);
    dl->AddText({textX, textY + lineH * 2.0f}, IM_COL32(80, 130, 255, 255), zBuf);
}

} // namespace dw

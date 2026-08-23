#include "viewport_panel.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>

#include <imgui.h>

#include "../../core/config/config.h"
#include "../../core/coordinate_utils.h"
#include "../../core/mesh/mesh.h"
#include "../context_menu_manager.h"

namespace dw {

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

    renderIdentityOverlay();

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

void ViewportPanel::renderIdentityOverlay() {
    const auto view = viewport::presentIdentity(m_presentationIdentity);
    if (!view.visible) {
        return;
    }

    const ImVec2 rectMin = ImGui::GetItemRectMin();
    const ImVec2 rectMax = ImGui::GetItemRectMax();
    const float padding = ImGui::GetStyle().FramePadding.x * 1.75f;
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    const float badgeWidth = ImGui::CalcTextSize(view.badge.c_str()).x;
    const float labelWidth = ImGui::CalcTextSize(view.label.c_str()).x;
    const float availableWidth = std::max(0.0f, rectMax.x - rectMin.x - padding * 4.0f);
    const float boxWidth = std::min(std::max(badgeWidth, labelWidth) + padding * 2.0f,
                                    availableWidth);
    if (boxWidth <= padding * 2.0f) {
        return;
    }

    const ImVec2 boxMin{rectMin.x + padding, rectMin.y + padding};
    const ImVec2 boxMax{boxMin.x + boxWidth, boxMin.y + lineHeight * 2.0f + padding * 2.0f};
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(boxMin, boxMax, IM_COL32(8, 10, 12, 225), 4.0f);
    drawList->AddRect(boxMin, boxMax, IM_COL32(110, 120, 130, 230), 4.0f);
    drawList->PushClipRect(boxMin, boxMax, true);
    drawList->AddText({boxMin.x + padding, boxMin.y + padding},
                      IM_COL32(170, 205, 255, 255),
                      view.badge.c_str());
    drawList->AddText({boxMin.x + padding, boxMin.y + padding + lineHeight},
                      IM_COL32(245, 245, 245, 255),
                      view.label.c_str());
    drawList->PopClipRect();
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

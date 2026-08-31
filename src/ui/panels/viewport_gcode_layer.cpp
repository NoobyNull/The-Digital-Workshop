#include "viewport_panel.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <utility>

#include <imgui.h>

#include "../../core/coordinate_utils.h"
#include "../../core/gcode/gcode_viewport_sampling.h"
#include "../../render/gl_utils.h"

namespace dw {

void ViewportPanel::setGCodeProgram(gcode::Program program,
                                    ViewportGCodeSource source) {
    m_gcodeProgram = std::move(program);
    m_gcodeSource = source;
    m_gcodeDirty = true;
    m_alignmentDirty = true;

    // Initialize Z-clip bounds from program
    m_zClipMaxBound = m_gcodeProgram.boundsMax.z;
    m_zClipMax = m_zClipMaxBound;

    // Fit camera to G-code bounds if no mesh is currently loaded
    if (!m_mesh) {
        // Swap Y<->Z: G-code uses Z-up, renderer uses Y-up
        Vec3 bMin = gcodeToRenderer(m_gcodeProgram.boundsMin);
        Vec3 bMax = gcodeToRenderer(m_gcodeProgram.boundsMax);
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
    m_gcodeSource = ViewportGCodeSource::None;
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

bool ViewportPanel::clearGCodeProgramIfSource(ViewportGCodeSource source) {
    if (m_gcodeSource != source) return false;
    clearGCodeProgram();
    return true;
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
} // namespace dw

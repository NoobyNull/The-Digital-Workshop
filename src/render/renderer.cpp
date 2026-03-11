#include "renderer.h"

#include <vector>

#include "../core/utils/log.h"
#include "gl_utils.h"
#include "shader_sources.h"

namespace dw {

void GPUMesh::destroy() {
    if (ebo != 0) {
        glDeleteBuffers(1, &ebo);
        ebo = 0;
    }
    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    indexCount = 0;
}

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::initialize() {
    if (m_initialized) {
        return true;
    }

    if (!createShaders()) {
        return false;
    }

    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Enable back-face culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Enable blending for grid fade
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Create helper meshes
    createGridMesh(20.0f, 1.0f);
    createAxisMesh(2.0f);

    m_initialized = true;
    log::info("Renderer", "Initialized");
    return true;
}

void Renderer::shutdown() {
    clearMeshCache();
    m_gridMesh.destroy();
    m_axisMesh.destroy();

    if (m_pointVBO != 0) { glDeleteBuffers(1, &m_pointVBO); m_pointVBO = 0; }
    if (m_pointVAO != 0) { glDeleteVertexArrays(1, &m_pointVAO); m_pointVAO = 0; }
    if (m_wireBoxVBO != 0) { glDeleteBuffers(1, &m_wireBoxVBO); m_wireBoxVBO = 0; }
    if (m_wireBoxVAO != 0) { glDeleteVertexArrays(1, &m_wireBoxVAO); m_wireBoxVAO = 0; }

    m_initialized = false;
}

void Renderer::beginFrame(const Color& clearColor) {
    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::endFrame() {
    // Nothing special needed
}

void Renderer::setCamera(const Camera& camera) {
    m_camera = camera;
}

void Renderer::renderMesh(const Mesh& mesh, const Mat4& modelMatrix) {
    renderMesh(mesh, nullptr, modelMatrix);
}

void Renderer::renderMesh(const GPUMesh& gpuMesh, const Mat4& modelMatrix) {
    renderMesh(gpuMesh, nullptr, modelMatrix);
}

void Renderer::renderMesh(const Mesh& mesh,
                          const Texture* materialTexture,
                          const Mat4& modelMatrix) {
    u64 key = mesh.geometryHash();
    auto it = m_meshCache.find(key);
    if (it == m_meshCache.end()) {
        evictMeshCache();
        it = m_meshCache.emplace(key, uploadMesh(mesh)).first;
    }
    renderMesh(it->second, materialTexture, modelMatrix);
}

void Renderer::renderMesh(const GPUMesh& gpuMesh,
                          const Texture* materialTexture,
                          const Mat4& modelMatrix) {
    if (gpuMesh.vao == 0 || gpuMesh.indexCount == 0) {
        return;
    }

    // Disable back-face culling so non-solid meshes (e.g. CNC relief surfaces) are
    // visible from both sides — same approach as renderToolpath()
    glDisable(GL_CULL_FACE);

    if (m_settings.wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    m_meshShader.bind();

    // Set uniforms
    m_meshShader.setMat4("uModel", modelMatrix);
    m_meshShader.setMat4("uView", m_camera.viewMatrix());
    m_meshShader.setMat4("uProjection", m_camera.projectionMatrix());

    // Calculate normal matrix: transpose(inverse(mat3(model))) for correct lighting
    // under non-uniform scaling
    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
    m_meshShader.setMat3("uNormalMatrix", normalMatrix);

    // Transform light direction into world space relative to camera orientation
    // so lighting stays consistent regardless of orbit angle
    Vec3 viewLightDir = glm::mat3(glm::inverse(m_camera.viewMatrix())) * m_settings.lightDir;
    m_meshShader.setVec3("uLightDir", viewLightDir);
    m_meshShader.setVec3("uLightColor", m_settings.lightColor);
    m_meshShader.setVec3("uAmbient", m_settings.ambient);
    m_meshShader.setVec3(
        "uObjectColor",
        Vec3{m_settings.objectColor.r, m_settings.objectColor.g, m_settings.objectColor.b});
    m_meshShader.setVec3("uViewPos", m_camera.position());
    m_meshShader.setFloat("uShininess", m_settings.shininess);
    m_meshShader.setBool("uIsToolpath", false);

    // Bind material texture if provided and valid
    bool useTexture = (materialTexture != nullptr && materialTexture->isValid());
    m_meshShader.setBool("uUseTexture", useTexture);
    if (useTexture) {
        materialTexture->bind(0);
        m_meshShader.setInt("uMaterialTexture", 0);
    }

    glBindVertexArray(gpuMesh.vao);
    glDrawElements(
        GL_TRIANGLES, static_cast<GLsizei>(gpuMesh.indexCount), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    // Unbind texture after draw to keep state clean
    if (useTexture) {
        materialTexture->unbind();
    }

    if (m_settings.wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    glEnable(GL_CULL_FACE);
}

void Renderer::renderToolpath(const Mesh& toolpathMesh, const Mat4& modelMatrix) {
    if (!toolpathMesh.isValid()) {
        return;
    }

    // Upload or get cached GPU mesh
    u64 key = toolpathMesh.geometryHash();
    auto it = m_meshCache.find(key);
    if (it == m_meshCache.end()) {
        evictMeshCache();
        it = m_meshCache.emplace(key, uploadMesh(toolpathMesh)).first;
    }

    const GPUMesh& gpuMesh = it->second;
    if (gpuMesh.vao == 0 || gpuMesh.indexCount == 0) {
        return;
    }

    // Disable back-face culling for toolpath (thin geometry visible from both sides)
    glDisable(GL_CULL_FACE);

    m_meshShader.bind();

    // Set standard mesh uniforms
    m_meshShader.setMat4("uModel", modelMatrix);
    m_meshShader.setMat4("uView", m_camera.viewMatrix());
    m_meshShader.setMat4("uProjection", m_camera.projectionMatrix());

    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
    m_meshShader.setMat3("uNormalMatrix", normalMatrix);

    Vec3 viewLightDir = glm::mat3(glm::inverse(m_camera.viewMatrix())) * m_settings.lightDir;
    m_meshShader.setVec3("uLightDir", viewLightDir);
    m_meshShader.setVec3("uLightColor", m_settings.lightColor);
    m_meshShader.setVec3("uAmbient", m_settings.ambient);
    m_meshShader.setVec3("uObjectColor", Vec3{1.0f, 1.0f, 1.0f}); // Will be overridden by shader
    m_meshShader.setVec3("uViewPos", m_camera.position());
    m_meshShader.setFloat("uShininess", m_settings.shininess);

    // Enable toolpath rendering mode (texture disabled)
    m_meshShader.setBool("uIsToolpath", true);
    m_meshShader.setBool("uUseTexture", false);

    glBindVertexArray(gpuMesh.vao);
    glDrawElements(
        GL_TRIANGLES, static_cast<GLsizei>(gpuMesh.indexCount), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    // Disable toolpath mode for subsequent renders
    m_meshShader.setBool("uIsToolpath", false);

    // Re-enable back-face culling
    glEnable(GL_CULL_FACE);
}

void Renderer::renderGrid(f32 size, [[maybe_unused]] f32 spacing) {
    if (!m_settings.showGrid || m_gridMesh.vao == 0) {
        return;
    }

    glDisable(GL_CULL_FACE);

    m_gridShader.bind();
    m_gridShader.setMat4("uMVP", m_camera.viewProjectionMatrix());
    m_gridShader.setVec4("uColor", Vec4{0.5f, 0.5f, 0.5f, 0.5f});
    m_gridShader.setFloat("uFadeStart", size * 0.5f);
    m_gridShader.setFloat("uFadeEnd", size);

    glBindVertexArray(m_gridMesh.vao);
    glDrawElements(GL_LINES, static_cast<GLsizei>(m_gridMesh.indexCount), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glEnable(GL_CULL_FACE);
}

void Renderer::renderAxis([[maybe_unused]] f32 length) {
    if (!m_settings.showAxis || m_axisMesh.vao == 0) {
        return;
    }

    glDisable(GL_DEPTH_TEST);

    m_flatShader.bind();
    m_flatShader.setMat4("uMVP", m_camera.viewProjectionMatrix());

    glBindVertexArray(m_axisMesh.vao);

    // X axis (red)
    m_flatShader.setVec4("uColor", Vec4{1.0f, 0.0f, 0.0f, 1.0f});
    glDrawArrays(GL_LINES, 0, 2);

    // Y axis (green)
    m_flatShader.setVec4("uColor", Vec4{0.0f, 1.0f, 0.0f, 1.0f});
    glDrawArrays(GL_LINES, 2, 2);

    // Z axis (blue)
    m_flatShader.setVec4("uColor", Vec4{0.0f, 0.0f, 1.0f, 1.0f});
    glDrawArrays(GL_LINES, 4, 2);

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

GPUMesh Renderer::uploadMesh(const Mesh& mesh) {
    GPUMesh gpuMesh;

    if (mesh.vertices().empty() || mesh.indices().empty()) {
        return gpuMesh;
    }

    GL_CHECK(glGenVertexArrays(1, &gpuMesh.vao));
    GL_CHECK(glGenBuffers(1, &gpuMesh.vbo));
    GL_CHECK(glGenBuffers(1, &gpuMesh.ebo));

    GL_CHECK(glBindVertexArray(gpuMesh.vao));

    // Upload vertices
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, gpuMesh.vbo));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER,
                          static_cast<GLsizeiptr>(mesh.vertices().size() * sizeof(Vertex)),
                          mesh.vertices().data(),
                          GL_STATIC_DRAW));

    // Upload indices
    GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpuMesh.ebo));
    GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                          static_cast<GLsizeiptr>(mesh.indices().size() * sizeof(u32)),
                          mesh.indices().data(),
                          GL_STATIC_DRAW));

    // Position attribute
    GL_CHECK(glVertexAttribPointer(0,
                                   3,
                                   GL_FLOAT,
                                   GL_FALSE,
                                   sizeof(Vertex),
                                   reinterpret_cast<void*>(offsetof(Vertex, position))));
    GL_CHECK(glEnableVertexAttribArray(0));

    // Normal attribute
    GL_CHECK(glVertexAttribPointer(1,
                                   3,
                                   GL_FLOAT,
                                   GL_FALSE,
                                   sizeof(Vertex),
                                   reinterpret_cast<void*>(offsetof(Vertex, normal))));
    GL_CHECK(glEnableVertexAttribArray(1));

    // Texture coordinate attribute
    GL_CHECK(glVertexAttribPointer(2,
                                   2,
                                   GL_FLOAT,
                                   GL_FALSE,
                                   sizeof(Vertex),
                                   reinterpret_cast<void*>(offsetof(Vertex, texCoord))));
    GL_CHECK(glEnableVertexAttribArray(2));

    GL_CHECK(glBindVertexArray(0));

    gpuMesh.indexCount = mesh.indexCount();

    return gpuMesh;
}

void Renderer::renderPoint(const Vec3& position, f32 pointSize, const Vec4& color) {
    // Lazy-init 1-vertex VAO/VBO
    if (m_pointVAO == 0) {
        glGenVertexArrays(1, &m_pointVAO);
        glGenBuffers(1, &m_pointVBO);
        glBindVertexArray(m_pointVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_pointVBO);
        glBufferData(GL_ARRAY_BUFFER, 3 * sizeof(f32), nullptr, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(f32), nullptr);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

    // Update position data
    f32 pos[3] = {position.x, position.y, position.z};
    glBindBuffer(GL_ARRAY_BUFFER, m_pointVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(pos), pos);

    glDisable(GL_DEPTH_TEST);

    m_flatShader.bind();
    m_flatShader.setMat4("uMVP", m_camera.viewProjectionMatrix());
    m_flatShader.setVec4("uColor", color);

    glBindVertexArray(m_pointVAO);
    glPointSize(pointSize);
    glDrawArrays(GL_POINTS, 0, 1);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderWireBox(const Vec3& min, const Vec3& max, const Vec4& color) {
    // Lazy-init 24-vertex VAO/VBO (12 edges)
    if (m_wireBoxVAO == 0) {
        glGenVertexArrays(1, &m_wireBoxVAO);
        glGenBuffers(1, &m_wireBoxVBO);
        glBindVertexArray(m_wireBoxVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_wireBoxVBO);
        glBufferData(GL_ARRAY_BUFFER, 24 * 3 * sizeof(f32), nullptr, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(f32), nullptr);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

    // Build 12 edges (24 vertices) from min/max corners
    f32 verts[72]; // 24 * 3
    auto v = [&](int i, f32 x, f32 y, f32 z) {
        verts[i * 3] = x;
        verts[i * 3 + 1] = y;
        verts[i * 3 + 2] = z;
    };
    // Bottom face edges
    v(0, min.x, min.y, min.z); v(1, max.x, min.y, min.z);
    v(2, max.x, min.y, min.z); v(3, max.x, min.y, max.z);
    v(4, max.x, min.y, max.z); v(5, min.x, min.y, max.z);
    v(6, min.x, min.y, max.z); v(7, min.x, min.y, min.z);
    // Top face edges
    v(8, min.x, max.y, min.z);  v(9, max.x, max.y, min.z);
    v(10, max.x, max.y, min.z); v(11, max.x, max.y, max.z);
    v(12, max.x, max.y, max.z); v(13, min.x, max.y, max.z);
    v(14, min.x, max.y, max.z); v(15, min.x, max.y, min.z);
    // Vertical edges
    v(16, min.x, min.y, min.z); v(17, min.x, max.y, min.z);
    v(18, max.x, min.y, min.z); v(19, max.x, max.y, min.z);
    v(20, max.x, min.y, max.z); v(21, max.x, max.y, max.z);
    v(22, min.x, min.y, max.z); v(23, min.x, max.y, max.z);

    glBindBuffer(GL_ARRAY_BUFFER, m_wireBoxVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    glDisable(GL_CULL_FACE);

    m_flatShader.bind();
    m_flatShader.setMat4("uMVP", m_camera.viewProjectionMatrix());
    m_flatShader.setVec4("uColor", color);

    glBindVertexArray(m_wireBoxVAO);
    glDrawArrays(GL_LINES, 0, 24);
    glBindVertexArray(0);

    glEnable(GL_CULL_FACE);
}

void Renderer::clearMeshCache() {
    for (auto& [key, gpu] : m_meshCache) {
        gpu.destroy();
    }
    m_meshCache.clear();
}

void Renderer::evictMeshCache() {
    if (m_meshCache.size() < MAX_CACHED_MESHES)
        return;
    // Simple eviction: drop half the cache when full.
    // More sophisticated LRU would require access tracking.
    size_t toRemove = m_meshCache.size() / 2;
    auto it = m_meshCache.begin();
    while (toRemove > 0 && it != m_meshCache.end()) {
        it->second.destroy();
        it = m_meshCache.erase(it);
        --toRemove;
    }
}

bool Renderer::createShaders() {
    if (!m_meshShader.compile(shaders::MESH_VERTEX, shaders::MESH_FRAGMENT)) {
        log::error("Renderer", "Failed to compile mesh shader");
        return false;
    }

    if (!m_flatShader.compile(shaders::FLAT_VERTEX, shaders::FLAT_FRAGMENT)) {
        log::error("Renderer", "Failed to compile flat shader");
        return false;
    }

    if (!m_gridShader.compile(shaders::GRID_VERTEX, shaders::GRID_FRAGMENT)) {
        log::error("Renderer", "Failed to compile grid shader");
        return false;
    }

    if (!m_heightLineShader.compile(shaders::HEIGHT_LINE_VERTEX, shaders::HEIGHT_LINE_FRAGMENT)) {
        log::error("Renderer", "Failed to compile height-line shader");
        return false;
    }

    return true;
}

void Renderer::createGridMesh(f32 size, f32 spacing) {
    std::vector<f32> vertices;
    std::vector<u32> indices;

    int halfLines = static_cast<int>(size / spacing);
    u32 idx = 0;

    for (int i = -halfLines; i <= halfLines; ++i) {
        f32 pos = static_cast<f32>(i) * spacing;

        // X parallel lines
        vertices.insert(vertices.end(), {-size, 0.0f, pos});
        vertices.insert(vertices.end(), {size, 0.0f, pos});
        indices.push_back(idx++);
        indices.push_back(idx++);

        // Z parallel lines
        vertices.insert(vertices.end(), {pos, 0.0f, -size});
        vertices.insert(vertices.end(), {pos, 0.0f, size});
        indices.push_back(idx++);
        indices.push_back(idx++);
    }

    glGenVertexArrays(1, &m_gridMesh.vao);
    glGenBuffers(1, &m_gridMesh.vbo);
    glGenBuffers(1, &m_gridMesh.ebo);

    glBindVertexArray(m_gridMesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_gridMesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(f32)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_gridMesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(u32)),
                 indices.data(),
                 GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(f32), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    m_gridMesh.indexCount = static_cast<u32>(indices.size());
}

void Renderer::createAxisMesh(f32 length) {
    f32 vertices[] = {
        // X axis
        0.0f,
        0.0f,
        0.0f,
        length,
        0.0f,
        0.0f,
        // Y axis
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        length,
        0.0f,
        // Z axis
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        length,
    };

    glGenVertexArrays(1, &m_axisMesh.vao);
    glGenBuffers(1, &m_axisMesh.vbo);

    glBindVertexArray(m_axisMesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_axisMesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(f32), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    m_axisMesh.indexCount = 6;
}

} // namespace dw

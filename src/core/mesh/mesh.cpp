#include "mesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../utils/log.h"

namespace dw {

namespace {

f32 upper3x3Determinant(const Mat4& matrix) {
    return matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[2][1] * matrix[1][2]) -
           matrix[1][0] * (matrix[0][1] * matrix[2][2] - matrix[2][1] * matrix[0][2]) +
           matrix[2][0] * (matrix[0][1] * matrix[1][2] - matrix[1][1] * matrix[0][2]);
}

Mat4 repairReflectedOrientation(Mat4 matrix) {
    if (upper3x3Determinant(matrix) < -1e-6f) {
        matrix = glm::scale(Mat4(1.0f), Vec3{1.0f, 1.0f, -1.0f}) * matrix;
    }
    return matrix;
}

} // namespace

Mesh::Mesh(std::vector<Vertex> vertices, std::vector<u32> indices)
    : m_vertices(std::move(vertices)), m_indices(std::move(indices)) {
    recalculateBounds();
}

void Mesh::clear() {
    m_vertices.clear();
    m_indices.clear();
    m_bounds.reset();
    m_name.clear();
    m_hashCached = false;
}

u64 Mesh::geometryHash() const {
    if (m_hashCached)
        return m_geometryHash;

    constexpr u64 FNV_OFFSET = 14695981039346656037ULL;
    constexpr u64 FNV_PRIME = 1099511628211ULL;

    u64 h = FNV_OFFSET;
    u32 vc = vertexCount();
    auto mix = [&](const void* data, size_t size) {
        const u8* p = static_cast<const u8*>(data);
        for (size_t i = 0; i < size; ++i) {
            h ^= p[i];
            h *= FNV_PRIME;
        }
    };

    mix(&vc, sizeof(vc));
    for (const auto& v : m_vertices) {
        mix(&v.position.x, sizeof(f32) * 3);
        mix(&v.normal.x, sizeof(f32) * 3);
        mix(&v.texCoord.x, sizeof(f32) * 2);
    }
    for (u32 idx : m_indices) {
        h ^= idx;
        h *= FNV_PRIME;
    }

    m_geometryHash = h;
    m_hashCached = true;
    return h;
}

void Mesh::recalculateBounds() {
    m_bounds.reset();
    for (const auto& vertex : m_vertices) {
        m_bounds.expand(vertex.position);
    }
}

void Mesh::recalculateNormals() {
    m_hashCached = false;

    // Reset all normals to zero
    for (auto& vertex : m_vertices) {
        vertex.normal = Vec3{0.0f, 0.0f, 0.0f};
    }

    // Accumulate face normals for each vertex
    for (usize i = 0; i + 2 < m_indices.size(); i += 3) {
        u32 i0 = m_indices[i];
        u32 i1 = m_indices[i + 1];
        u32 i2 = m_indices[i + 2];

        const Vec3& v0 = m_vertices[i0].position;
        const Vec3& v1 = m_vertices[i1].position;
        const Vec3& v2 = m_vertices[i2].position;

        Vec3 edge1 = v1 - v0;
        Vec3 edge2 = v2 - v0;
        Vec3 normal = glm::cross(edge1, edge2);

        m_vertices[i0].normal = m_vertices[i0].normal + normal;
        m_vertices[i1].normal = m_vertices[i1].normal + normal;
        m_vertices[i2].normal = m_vertices[i2].normal + normal;
    }

    // Normalize all normals (guard against zero-length from degenerate triangles)
    for (auto& vertex : m_vertices) {
        f32 len = glm::length(vertex.normal);
        if (len > 1e-7f) {
            vertex.normal = vertex.normal / len;
        } else {
            vertex.normal = Vec3{0.0f, 1.0f, 0.0f}; // Default up normal
        }
    }
}

void Mesh::transform(const Mat4& matrix) {
    m_hashCached = false;
    // Normal matrix (inverse transpose of upper 3x3)
    // For simple transforms, we can use the matrix directly
    for (auto& vertex : m_vertices) {
        Vec4 pos = matrix * Vec4(vertex.position, 1.0f);
        vertex.position = Vec3{pos.x, pos.y, pos.z};

        // Transform normal (assuming no non-uniform scaling)
        Vec4 norm = matrix * Vec4(vertex.normal, 0.0f);
        Vec3 n{norm.x, norm.y, norm.z};
        f32 len = glm::length(n);
        vertex.normal = (len > 1e-7f) ? (n / len) : Vec3{0.0f, 1.0f, 0.0f};
    }

    recalculateBounds();
}

void Mesh::centerOnOrigin() {
    m_hashCached = false;
    Vec3 center = m_bounds.center();

    for (auto& vertex : m_vertices) {
        vertex.position = vertex.position - center;
    }

    // Update bounds mathematically instead of full vertex iteration
    m_bounds.min = m_bounds.min - center;
    m_bounds.max = m_bounds.max - center;
}

void Mesh::normalizeSize(f32 targetSize) {
    f32 maxExtent = m_bounds.maxExtent();
    if (maxExtent > 0.0f) {
        f32 scale = targetSize / maxExtent;
        Mat4 scaleMatrix = glm::scale(Mat4(1.0f), Vec3{scale, scale, scale});
        transform(scaleMatrix);
    }
}

f32 Mesh::autoOrient() {
    if (!isValid()) {
        return 0.0f;
    }

    recalculateBounds();
    Vec3 sz = m_bounds.size();
    f32 extents[3] = {sz.x, sz.y, sz.z};

    // Find thinnest axis (depth), tallest of remaining (height), third (width)
    // Axes: 0=X, 1=Y, 2=Z
    int depthAxis = 0;
    if (extents[1] < extents[depthAxis])
        depthAxis = 1;
    if (extents[2] < extents[depthAxis])
        depthAxis = 2;

    int heightAxis = -1;
    int widthAxis = -1;
    for (int i = 0; i < 3; i++) {
        if (i == depthAxis)
            continue;
        if (heightAxis < 0 || extents[i] > extents[heightAxis]) {
            widthAxis = heightAxis;
            heightAxis = i;
        } else {
            widthAxis = i;
        }
    }

    // Build an orientation matrix: widthAxis->X, heightAxis->Y, depthAxis->Z.
    // A single axis swap is a reflection, so flip only the depth axis when the
    // permutation is odd. That preserves the visible face's X/Y text layout.
    bool needsPermute = (widthAxis != 0 || heightAxis != 1 || depthAxis != 2);
    if (needsPermute) {
        Mat4 perm = Mat4(1.0f);
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                perm[c][r] = 0.0f;

        int inversions = 0;
        int axes[3] = {widthAxis, heightAxis, depthAxis};
        for (int i = 0; i < 3; ++i) {
            for (int j = i + 1; j < 3; ++j) {
                if (axes[i] > axes[j])
                    ++inversions;
            }
        }
        f32 depthSign = (inversions % 2 == 0) ? 1.0f : -1.0f;

        perm[widthAxis][0] = 1.0f;
        perm[heightAxis][1] = 1.0f;
        perm[depthAxis][2] = depthSign;

        transform(perm);
        m_orientMatrix = perm;
    } else {
        m_orientMatrix = Mat4(1.0f);
    }
    m_autoOriented = true;

    // Count triangle normals along Z to determine front face
    i32 posZ = 0;
    i32 negZ = 0;
    for (usize i = 0; i + 2 < m_indices.size(); i += 3) {
        const Vec3& v0 = m_vertices[m_indices[i]].position;
        const Vec3& v1 = m_vertices[m_indices[i + 1]].position;
        const Vec3& v2 = m_vertices[m_indices[i + 2]].position;

        Vec3 edge1 = v1 - v0;
        Vec3 edge2 = v2 - v0;
        Vec3 normal = glm::cross(edge1, edge2);

        if (normal.z > 0.0f)
            posZ++;
        else if (normal.z < 0.0f)
            negZ++;
    }

    return (posZ >= negZ) ? 0.0f : 180.0f;
}

void Mesh::applyStoredOrient(const Mat4& matrix) {
    if (!isValid()) {
        return;
    }

    Mat4 repaired = repairReflectedOrientation(matrix);

    // Only apply if not identity
    if (repaired != Mat4(1.0f)) {
        transform(repaired);
    }
    m_orientMatrix = repaired;
    m_autoOriented = true;
}

void Mesh::revertAutoOrient() {
    if (!m_autoOriented) {
        return;
    }

    // Inverse of a signed axis-orientation matrix is its transpose.
    Mat4 inv = Mat4(1.0f);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            inv[c][r] = m_orientMatrix[r][c];

    transform(inv);
    m_autoOriented = false;
    m_orientMatrix = Mat4(1.0f);
}

void Mesh::merge(const Mesh& other) {
    m_hashCached = false;
    u32 vertexOffset = static_cast<u32>(m_vertices.size());

    // Add vertices
    m_vertices.insert(m_vertices.end(), other.m_vertices.begin(), other.m_vertices.end());

    // Add indices with offset
    for (u32 index : other.m_indices) {
        m_indices.push_back(index + vertexOffset);
    }

    // Update bounds
    m_bounds.expand(other.m_bounds);
}

Mesh Mesh::clone() const {
    Mesh copy;
    copy.m_vertices = m_vertices;
    copy.m_indices = m_indices;
    copy.m_bounds = m_bounds;
    copy.m_name = m_name;
    copy.m_orientMatrix = m_orientMatrix;
    copy.m_autoOriented = m_autoOriented;
    return copy;
}

bool Mesh::isValid() const {
    return !m_vertices.empty() && !m_indices.empty() && m_indices.size() % 3 == 0;
}

bool Mesh::hasNormals() const {
    if (m_vertices.empty()) {
        return false;
    }

    // Check if any normal is non-zero
    for (const auto& vertex : m_vertices) {
        if (vertex.normal.x != 0.0f || vertex.normal.y != 0.0f || vertex.normal.z != 0.0f) {
            return true;
        }
    }
    return false;
}

bool Mesh::needsUVGeneration() const {
    if (m_vertices.empty()) {
        return true;
    }
    return std::all_of(m_vertices.begin(), m_vertices.end(), [](const Vertex& v) {
        return std::fabs(v.texCoord.x) < 0.0001f && std::fabs(v.texCoord.y) < 0.0001f;
    });
}

void Mesh::generatePlanarUVs(float grainRotationDeg) {
    if (m_vertices.empty()) {
        return;
    }
    m_hashCached = false;

    // Ensure bounds are current
    recalculateBounds();

    Vec3 size = m_bounds.size();

    // Determine projection plane by picking the two largest dimensions.
    // The smallest dimension is the "depth" axis (perpendicular to projection).
    // Projection plane candidates and their areas:
    //   XY plane: size.x * size.y
    //   XZ plane: size.x * size.z
    //   YZ plane: size.y * size.z
    float areaXY = size.x * size.y;
    float areaXZ = size.x * size.z;
    float areaYZ = size.y * size.z;

    // axis1/axis2 index into position: 0=X, 1=Y, 2=Z
    int axis1 = 0;
    int axis2 = 1;
    float axis1Size = size.x;
    float axis2Size = size.y;

    if (areaXZ >= areaXY && areaXZ >= areaYZ) {
        // XZ plane
        axis1 = 0;
        axis2 = 2;
        axis1Size = size.x;
        axis2Size = size.z;
    } else if (areaYZ >= areaXY && areaYZ >= areaXZ) {
        // YZ plane
        axis1 = 1;
        axis2 = 2;
        axis1Size = size.y;
        axis2Size = size.z;
    }
    // else XY plane (default, already set)

    // Guard against degenerate meshes (zero-size dimensions)
    if (axis1Size < 1e-6f)
        axis1Size = 1.0f;
    if (axis2Size < 1e-6f)
        axis2Size = 1.0f;

    auto getAxis = [](const Vec3& v, int axis) -> float {
        if (axis == 0)
            return v.x;
        if (axis == 1)
            return v.y;
        return v.z;
    };

    float minAxis1 = getAxis(m_bounds.min, axis1);
    float minAxis2 = getAxis(m_bounds.min, axis2);

    bool doRotate = std::fabs(grainRotationDeg) > 0.0001f;
    float rad = 0.0f;
    float cosR = 1.0f, sinR = 0.0f;
    if (doRotate) {
        // glm::radians equivalent without including all of GLM
        rad = grainRotationDeg * 3.14159265358979323846f / 180.0f;
        cosR = std::cos(rad);
        sinR = std::sin(rad);
    }

    for (auto& vertex : m_vertices) {
        float u = (getAxis(vertex.position, axis1) - minAxis1) / axis1Size;
        float v = (getAxis(vertex.position, axis2) - minAxis2) / axis2Size;

        if (doRotate) {
            // Rotate around UV center (0.5, 0.5)
            float du = u - 0.5f;
            float dv = v - 0.5f;
            u = du * cosR - dv * sinR + 0.5f;
            v = du * sinR + dv * cosR + 0.5f;
        }

        vertex.texCoord = Vec2{u, v};
    }
}

bool Mesh::hasTexCoords() const {
    if (m_vertices.empty()) {
        return false;
    }

    // Check if any texcoord is non-zero
    for (const auto& vertex : m_vertices) {
        if (vertex.texCoord.x != 0.0f || vertex.texCoord.y != 0.0f) {
            return true;
        }
    }
    return false;
}

bool Mesh::validate() const {
    bool valid = true;
    u32 vertCount = vertexCount();

    // Check for NaN/Inf in vertex positions and normals
    u32 nanVerts = 0;
    for (const auto& v : m_vertices) {
        if (std::isnan(v.position.x) || std::isnan(v.position.y) || std::isnan(v.position.z) ||
            std::isinf(v.position.x) || std::isinf(v.position.y) || std::isinf(v.position.z)) {
            nanVerts++;
        }
    }
    if (nanVerts > 0) {
        log::warningf("Mesh", "%u vertices have NaN/Inf positions", nanVerts);
        valid = false;
    }

    // Check for out-of-bounds indices
    u32 oobIndices = 0;
    for (u32 idx : m_indices) {
        if (idx >= vertCount) {
            oobIndices++;
        }
    }
    if (oobIndices > 0) {
        log::warningf("Mesh", "%u indices out of bounds (vertex count: %u)", oobIndices, vertCount);
        valid = false;
    }

    // Check for degenerate triangles (zero area)
    u32 degenerates = 0;
    for (usize i = 0; i + 2 < m_indices.size(); i += 3) {
        u32 i0 = m_indices[i], i1 = m_indices[i + 1], i2 = m_indices[i + 2];
        if (i0 >= vertCount || i1 >= vertCount || i2 >= vertCount) {
            continue; // Already flagged above
        }
        if (i0 == i1 || i1 == i2 || i0 == i2) {
            degenerates++;
            continue;
        }
        Vec3 e1 = m_vertices[i1].position - m_vertices[i0].position;
        Vec3 e2 = m_vertices[i2].position - m_vertices[i0].position;
        Vec3 cross = glm::cross(e1, e2);
        if (glm::dot(cross, cross) < 1e-12f) {
            degenerates++;
        }
    }
    if (degenerates > 0) {
        log::warningf("Mesh", "%u degenerate triangles (zero area)", degenerates);
        valid = false;
    }

    return valid;
}

bool Mesh::validateGeometry() const {
    bool valid = true;
    u32 vertCount = vertexCount();

    // Check for NaN/Inf in vertex positions (fatal - makes mesh unrenderable)
    u32 nanVerts = 0;
    for (const auto& v : m_vertices) {
        if (std::isnan(v.position.x) || std::isnan(v.position.y) || std::isnan(v.position.z) ||
            std::isinf(v.position.x) || std::isinf(v.position.y) || std::isinf(v.position.z)) {
            nanVerts++;
        }
    }
    if (nanVerts > 0) {
        log::warningf("Mesh", "%u vertices have NaN/Inf positions", nanVerts);
        valid = false;
    }

    // Check for out-of-bounds indices (fatal - causes crashes)
    u32 oobIndices = 0;
    for (u32 idx : m_indices) {
        if (idx >= vertCount) {
            oobIndices++;
        }
    }
    if (oobIndices > 0) {
        log::warningf("Mesh", "%u indices out of bounds (vertex count: %u)", oobIndices, vertCount);
        valid = false;
    }

    // Note: Degenerate triangles are NOT checked here.
    // They are common in real-world meshes and don't prevent rendering.
    // Use validate() for a full check including degenerates.

    return valid;
}

void Mesh::reserve(u32 vertexCount, u32 indexCount) {
    m_vertices.reserve(vertexCount);
    m_indices.reserve(indexCount);
}

void Mesh::addVertex(const Vertex& vertex) {
    m_vertices.push_back(vertex);
    m_bounds.expand(vertex.position);
    m_hashCached = false;
}

void Mesh::addTriangle(u32 v0, u32 v1, u32 v2) {
    m_indices.push_back(v0);
    m_indices.push_back(v1);
    m_indices.push_back(v2);
    m_hashCached = false;
}

} // namespace dw

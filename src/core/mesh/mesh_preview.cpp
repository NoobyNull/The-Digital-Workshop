#include "mesh_preview.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace dw::mesh_preview {
namespace {

struct Projection {
    int a = 0;
    int b = 1;
    float minA = 0.0f;
    float minB = 0.0f;
    float spanA = 1.0f;
    float spanB = 1.0f;
};

float component(const Vec3& value, int axis) {
    switch (axis) {
    case 0: return value.x;
    case 1: return value.y;
    default: return value.z;
    }
}

Projection chooseProjection(const Mesh& mesh) {
    const auto& bounds = mesh.bounds();
    const Vec3 size = bounds.size();
    const float xy = std::abs(size.x * size.y);
    const float xz = std::abs(size.x * size.z);
    const float yz = std::abs(size.y * size.z);

    Projection projection;
    if (xz > xy && xz >= yz) {
        projection.a = 0;
        projection.b = 2;
    } else if (yz > xy && yz > xz) {
        projection.a = 1;
        projection.b = 2;
    }

    projection.minA = component(bounds.min, projection.a);
    projection.minB = component(bounds.min, projection.b);
    projection.spanA = std::max(component(bounds.max, projection.a) - projection.minA, 1e-6f);
    projection.spanB = std::max(component(bounds.max, projection.b) - projection.minB, 1e-6f);
    return projection;
}

std::pair<std::size_t, std::size_t> chooseGrid(const Projection& projection,
                                               std::size_t maxTriangles) {
    const double aspect = std::max(
        static_cast<double>(projection.spanA) / static_cast<double>(projection.spanB),
        1e-6);
    auto cellsA = static_cast<std::size_t>(
        std::ceil(std::sqrt(static_cast<double>(maxTriangles) * aspect)));
    cellsA = std::clamp<std::size_t>(cellsA, 1, maxTriangles);
    auto cellsB = std::max<std::size_t>(1, maxTriangles / cellsA);
    while (cellsA * cellsB < maxTriangles)
        ++cellsB;
    return {cellsA, cellsB};
}

std::size_t cellForTriangle(const std::vector<Vertex>& vertices,
                            const std::vector<u32>& indices,
                            std::size_t triangle,
                            const Projection& projection,
                            std::size_t cellsA,
                            std::size_t cellsB) {
    const std::size_t offset = triangle * 3;
    Vec3 centroid{0.0f, 0.0f, 0.0f};
    for (std::size_t corner = 0; corner < 3; ++corner)
        centroid += vertices[indices[offset + corner]].position;
    centroid /= 3.0f;

    const float normA = std::clamp(
        (component(centroid, projection.a) - projection.minA) / projection.spanA,
        0.0f,
        1.0f);
    const float normB = std::clamp(
        (component(centroid, projection.b) - projection.minB) / projection.spanB,
        0.0f,
        1.0f);

    auto cellA = std::min<std::size_t>(
        static_cast<std::size_t>(normA * static_cast<float>(cellsA)), cellsA - 1);
    auto cellB = std::min<std::size_t>(
        static_cast<std::size_t>(normB * static_cast<float>(cellsB)), cellsB - 1);
    return cellB * cellsA + cellA;
}

} // namespace

Mesh sampleTrianglesForPreview(const Mesh& mesh, std::size_t maxTriangles) {
    if (maxTriangles == 0 || mesh.triangleCount() <= maxTriangles)
        return mesh.clone();

    const auto& sourceVertices = mesh.vertices();
    const auto& sourceIndices = mesh.indices();
    const std::size_t sourceTriangles = sourceIndices.size() / 3;
    if (sourceTriangles == 0)
        return {};

    const Projection projection = chooseProjection(mesh);
    const auto [cellsA, cellsB] = chooseGrid(projection, maxTriangles);
    const std::size_t empty = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> selectedByCell(cellsA * cellsB, empty);
    std::size_t selectedCount = 0;

    for (std::size_t tri = 0; tri < sourceTriangles && selectedCount < maxTriangles; ++tri) {
        const std::size_t indexOffset = tri * 3;
        bool valid = true;
        for (std::size_t corner = 0; corner < 3; ++corner) {
            if (sourceIndices[indexOffset + corner] >= sourceVertices.size()) {
                valid = false;
                break;
            }
        }
        if (!valid)
            return {};

        const std::size_t cell =
            cellForTriangle(sourceVertices, sourceIndices, tri, projection, cellsA, cellsB);
        if (selectedByCell[cell] == empty) {
            selectedByCell[cell] = tri;
            ++selectedCount;
        }
    }

    auto forceSelect = [&](std::size_t tri) {
        const std::size_t cell =
            cellForTriangle(sourceVertices, sourceIndices, tri, projection, cellsA, cellsB);
        selectedByCell[cell] = tri;
    };
    forceSelect(0);
    forceSelect(sourceTriangles - 1);

    std::vector<Vertex> previewVertices;
    std::vector<u32> previewIndices;
    previewVertices.reserve(selectedCount * 3);
    previewIndices.reserve(selectedCount * 3);

    for (std::size_t tri : selectedByCell) {
        if (tri == empty)
            continue;
        const std::size_t indexOffset = tri * 3;
        const u32 base = static_cast<u32>(previewVertices.size());
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const u32 sourceIndex = sourceIndices[indexOffset + corner];
            previewVertices.push_back(sourceVertices[sourceIndex]);
            previewIndices.push_back(base + static_cast<u32>(corner));
        }
    }

    Mesh preview(std::move(previewVertices), std::move(previewIndices));
    preview.setName(mesh.name());
    return preview;
}

} // namespace dw::mesh_preview

#include "stl_loader.h"

#include <cmath>
#include <cstring>
#include <sstream>
#include <unordered_map>

#include "../utils/file_utils.h"
#include "../utils/log.h"
#include "../utils/string_utils.h"

namespace dw {

LoadResult STLLoader::load(const Path& path) {
    auto data = file::readBinary(path);
    if (!data) {
        return LoadResult{nullptr, "Failed to read file"};
    }

    // Detect format
    if (isBinary(*data)) {
        return loadBinary(*data);
    }

    // Try ASCII
    std::string content(reinterpret_cast<const char*>(data->data()), data->size());
    return loadAscii(content);
}

LoadResult STLLoader::loadFromBuffer(const ByteBuffer& data) {
    if (data.empty()) {
        return LoadResult{nullptr, "Empty buffer"};
    }

    if (isBinary(data)) {
        return loadBinary(data);
    }

    std::string content(reinterpret_cast<const char*>(data.data()), data.size());
    return loadAscii(content);
}

bool STLLoader::supports(const std::string& extension) const {
    return str::toLower(extension) == "stl";
}

std::vector<std::string> STLLoader::extensions() const {
    return {"stl"};
}

bool STLLoader::isBinary(const ByteBuffer& data) {
    if (data.size() < 84) {
        return false; // Too small for binary STL
    }

    // Check if it starts with "solid" (ASCII) but also has binary header
    // Binary STL has: 80 byte header + 4 byte triangle count
    std::string start(reinterpret_cast<const char*>(data.data()),
                      std::min(data.size(), static_cast<usize>(6)));

    if (str::toLower(start).find("solid") == 0) {
        // Could be ASCII - check if file size matches binary format
        u32 triangleCount = 0;
        std::memcpy(&triangleCount, data.data() + 80, sizeof(triangleCount));

        // Binary STL size = 80 + 4 + (triangleCount * 50)
        usize expectedSize = 84 + static_cast<usize>(triangleCount) * 50;

        // If size doesn't match binary format, it's likely ASCII
        if (data.size() != expectedSize) {
            return false;
        }
    }

    return true;
}

LoadResult STLLoader::loadBinary(const ByteBuffer& data) {
    if (data.size() < 84) {
        return LoadResult{nullptr, "File too small for binary STL"};
    }

    // Skip 80-byte header
    const u8* ptr = data.data() + 80;

    // Read triangle count
    u32 triangleCount = 0;
    std::memcpy(&triangleCount, ptr, sizeof(triangleCount));
    ptr += 4;

    // Handle empty STL
    if (triangleCount == 0) {
        return LoadResult{nullptr, "STL file contains no geometry (0 triangles)"};
    }

    // Guard against integer overflow: triangleCount * 50 must not wrap
    if (triangleCount > (SIZE_MAX - 84) / 50) {
        return LoadResult{nullptr, "Invalid binary STL: triangle count causes overflow"};
    }

    // Validate size
    usize expectedSize = 84 + static_cast<usize>(triangleCount) * 50;
    if (data.size() < expectedSize) {
        std::ostringstream oss;
        oss << "STL file truncated: expected " << triangleCount << " triangles but file too short";
        return LoadResult{nullptr, oss.str()};
    }

    // Binary STL stores per-face normals, so shared-position vertices have different
    // normals and almost never deduplicate. Skip dedup — build flat arrays directly.
    const u32 vertexCount = triangleCount * 3;
    std::vector<Vertex> vertices(vertexCount);
    std::vector<u32> indices(vertexCount);

    // Each binary STL triangle: 12 bytes normal + 3×12 bytes vertices + 2 bytes attr = 50 bytes
    for (u32 i = 0; i < triangleCount; ++i) {
        float triData[12]; // normal(3) + v0(3) + v1(3) + v2(3)
        std::memcpy(triData, ptr, 48);
        ptr += 50; // 48 bytes geometry + 2 bytes attribute

        // Validate normal
        if (!std::isfinite(triData[0]) || !std::isfinite(triData[1]) ||
            !std::isfinite(triData[2])) {
            return LoadResult{nullptr, "STL contains invalid normal data (NaN or Inf values)"};
        }

        Vec3 normal{triData[0], triData[1], triData[2]};
        u32 base = i * 3;

        // 3 vertices starting at triData[3]
        for (u32 j = 0; j < 3; ++j) {
            u32 off = 3 + j * 3;
            if (!std::isfinite(triData[off]) || !std::isfinite(triData[off + 1]) ||
                !std::isfinite(triData[off + 2])) {
                return LoadResult{nullptr, "STL contains invalid vertex data (NaN or Inf values)"};
            }
            vertices[base + j] = Vertex{Vec3{triData[off], triData[off + 1], triData[off + 2]},
                                        normal};
            indices[base + j] = base + j;
        }
    }

    auto mesh = std::make_shared<Mesh>(std::move(vertices), std::move(indices));
    mesh->recalculateNormals();

    log::infof("STL",
               "Loaded binary: %u vertices, %u triangles",
               mesh->vertexCount(),
               mesh->triangleCount());

    // Validate mesh integrity (only fatal issues: NaN/Inf, OOB indices)
    if (!mesh->validateGeometry()) {
        return LoadResult{
            nullptr,
            "Mesh validation failed: invalid NaN/Inf vertex positions or out-of-bounds indices"};
    }
    mesh->validate(); // Log warnings for degenerate triangles (non-fatal)

    return LoadResult{mesh, ""};
}

LoadResult STLLoader::loadAscii(const std::string& content) {
    auto mesh = std::make_shared<Mesh>();
    std::unordered_map<Vertex, u32> vertexMap;

    std::istringstream stream(content);
    std::string line;
    Vec3 currentNormal{0.0f, 0.0f, 1.0f};
    std::vector<u32> faceIndices;
    int lineNumber = 0;

    while (std::getline(stream, line)) {
        lineNumber++;
        line = str::trim(line);
        if (line.empty()) {
            continue;
        }

        std::string lower = str::toLower(line);

        if (str::startsWith(lower, "facet normal")) {
            // Parse normal
            try {
                std::istringstream ns(line.substr(12));
                ns >> currentNormal.x >> currentNormal.y >> currentNormal.z;

                // Validate normal
                if (!std::isfinite(currentNormal.x) || !std::isfinite(currentNormal.y) ||
                    !std::isfinite(currentNormal.z)) {
                    std::ostringstream oss;
                    oss << "STL contains invalid normal data at line " << lineNumber
                        << " (NaN or Inf values)";
                    return LoadResult{nullptr, oss.str()};
                }
            } catch (const std::exception& e) {
                log::warningf("STL", "Malformed normal at line %d, using default", lineNumber);
                currentNormal = Vec3{0.0f, 0.0f, 1.0f};
            }
            faceIndices.clear();
        } else if (str::startsWith(lower, "vertex")) {
            // Parse vertex
            try {
                std::istringstream vs(line.substr(6));
                Vertex v;
                vs >> v.position.x >> v.position.y >> v.position.z;
                v.normal = currentNormal;

                // Validate vertex
                if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) ||
                    !std::isfinite(v.position.z)) {
                    std::ostringstream oss;
                    oss << "STL contains invalid vertex data at line " << lineNumber
                        << " (NaN or Inf values)";
                    return LoadResult{nullptr, oss.str()};
                }

                // Bounds check
                constexpr f32 MAX_COORD = 1e6f;
                if (std::abs(v.position.x) > MAX_COORD || std::abs(v.position.y) > MAX_COORD ||
                    std::abs(v.position.z) > MAX_COORD) {
                    std::ostringstream oss;
                    oss << "STL contains extreme coordinates at line " << lineNumber
                        << " (>1e6), likely corrupt";
                    return LoadResult{nullptr, oss.str()};
                }

                // Deduplicate
                auto it = vertexMap.find(v);
                if (it != vertexMap.end()) {
                    faceIndices.push_back(it->second);
                } else {
                    u32 index = mesh->vertexCount();
                    vertexMap[v] = index;
                    mesh->addVertex(v);
                    faceIndices.push_back(index);
                }
            } catch (const std::exception& e) {
                log::warningf("STL", "Malformed vertex at line %d, skipping", lineNumber);
            }
        } else if (str::startsWith(lower, "endfacet")) {
            // Add triangle
            if (faceIndices.size() >= 3) {
                mesh->addTriangle(faceIndices[0], faceIndices[1], faceIndices[2]);
            }
        }
    }

    if (mesh->triangleCount() == 0) {
        return LoadResult{nullptr, "No triangles found in ASCII STL"};
    }

    mesh->recalculateNormals();
    mesh->recalculateBounds();

    log::infof("STL",
               "Loaded ASCII: %u vertices, %u triangles",
               mesh->vertexCount(),
               mesh->triangleCount());

    // Validate mesh integrity (only fatal issues: NaN/Inf, OOB indices)
    if (!mesh->validateGeometry()) {
        return LoadResult{
            nullptr,
            "Mesh validation failed: invalid NaN/Inf vertex positions or out-of-bounds indices"};
    }
    mesh->validate(); // Log warnings for degenerate triangles (non-fatal)

    return LoadResult{mesh, ""};
}

} // namespace dw

#include "threemf_loader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

#include <zlib.h>

#include "../utils/file_utils.h"
#include "../utils/log.h"
#include "../utils/string_utils.h"

namespace dw {

namespace {

// ZIP file structures (simplified)
#pragma pack(push, 1)
struct ZipLocalFileHeader {
    uint32_t signature; // 0x04034b50
    uint16_t versionNeeded;
    uint16_t flags;
    uint16_t compression;
    uint16_t modTime;
    uint16_t modDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t fileNameLength;
    uint16_t extraFieldLength;
};
#pragma pack(pop)

constexpr uint32_t ZIP_LOCAL_HEADER_SIG = 0x04034b50;

// Simple inflate for uncompressed (stored) ZIP entries
bool extractStoredEntry(std::istream& in, uint32_t size, std::string& out) {
    out.resize(size);
    in.read(out.data(), size);
    return in.good();
}

// Decompress a raw deflate stream (ZIP compression method 8)
bool decompressDeflate(const char* compressedData,
                       uint32_t compressedSize,
                       uint32_t uncompressedSize,
                       std::string& output) {
    output.resize(uncompressedSize);

    z_stream strm = {};
    // Use -MAX_WBITS for raw deflate (no zlib/gzip header) as used in ZIP files
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
        log::errorf("3MF", "Failed to initialize zlib inflate");
        return false;
    }

    strm.avail_in = compressedSize;
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressedData));
    strm.avail_out = uncompressedSize;
    strm.next_out = reinterpret_cast<Bytef*>(output.data());

    int ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        log::errorf("3MF", "Deflate decompression failed (zlib error %d)", ret);
        output.clear();
        return false;
    }

    return true;
}

// Find and extract a file from a ZIP archive
bool extractFromZip(const Path& zipPath, const std::string& targetFile, std::string& content) {
    std::ifstream file(zipPath, std::ios::binary);
    if (!file) {
        return false;
    }

    while (file.good()) {
        ZipLocalFileHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));

        if (!file.good() || header.signature != ZIP_LOCAL_HEADER_SIG) {
            break;
        }

        // Read filename
        std::string fileName(header.fileNameLength, '\0');
        file.read(fileName.data(), header.fileNameLength);

        // Skip extra field
        file.seekg(header.extraFieldLength, std::ios::cur);

        // Check if this is our target file
        bool isTarget = (fileName == targetFile) ||
                        (fileName.find(targetFile) != std::string::npos);

        if (isTarget && header.compression == 0) {
            // Uncompressed - extract directly
            return extractStoredEntry(file, header.uncompressedSize, content);
        } else if (isTarget && header.compression == 8) {
            // Deflate compression - read compressed data then decompress
            std::string compressed(header.compressedSize, '\0');
            file.read(compressed.data(), header.compressedSize);
            if (!file.good()) {
                log::errorf("3MF", "Failed to read compressed data from ZIP");
                return false;
            }
            return decompressDeflate(
                compressed.data(), header.compressedSize, header.uncompressedSize, content);
        }

        // Skip to next entry
        file.seekg(header.compressedSize, std::ios::cur);
    }

    return false;
}

// Extract a file from a ZIP archive stored in a byte buffer
bool extractFromZipBuffer(const ByteBuffer& zipData,
                          const std::string& targetFile,
                          std::string& content) {
    const u8* ptr = zipData.data();
    const u8* end = ptr + zipData.size();

    while (ptr + sizeof(ZipLocalFileHeader) <= end) {
        // Use memcpy to avoid strict aliasing violation (u8* -> struct*)
        ZipLocalFileHeader header;
        std::memcpy(&header, ptr, sizeof(header));

        if (header.signature != ZIP_LOCAL_HEADER_SIG) {
            break;
        }

        ptr += sizeof(ZipLocalFileHeader);

        if (ptr + header.fileNameLength > end)
            break;

        std::string fileName(reinterpret_cast<const char*>(ptr), header.fileNameLength);
        ptr += header.fileNameLength;

        // Skip extra field
        if (ptr + header.extraFieldLength > end)
            break;
        ptr += header.extraFieldLength;

        // Check if this is our target file
        bool isTarget = (fileName == targetFile) ||
                        (fileName.find(targetFile) != std::string::npos);

        if (isTarget && header.compression == 0) {
            if (ptr + header.uncompressedSize > end)
                break;
            content.assign(reinterpret_cast<const char*>(ptr), header.uncompressedSize);
            return true;
        } else if (isTarget && header.compression == 8) {
            if (ptr + header.compressedSize > end)
                break;
            return decompressDeflate(reinterpret_cast<const char*>(ptr),
                                     header.compressedSize,
                                     header.uncompressedSize,
                                     content);
        }

        // Skip to next entry
        if (ptr + header.compressedSize > end)
            break;
        ptr += header.compressedSize;
    }

    return false;
}

// Simple XML attribute extraction
std::string getXmlAttribute(const std::string& tag, const std::string& attr) {
    std::string search = attr + "=\"";
    size_t pos = tag.find(search);
    if (pos == std::string::npos) {
        return "";
    }
    pos += search.length();
    size_t end = tag.find('"', pos);
    if (end == std::string::npos) {
        return "";
    }
    return tag.substr(pos, end - pos);
}

} // namespace

LoadResult ThreeMFLoader::load(const Path& path) {
    LoadResult result;

    // Validate file exists and has minimum size for a ZIP
    auto fileSize = file::getFileSize(path);
    if (!fileSize || *fileSize < 22) {
        result.error = "3MF file has invalid archive structure (too small for ZIP)";
        return result;
    }

    // Try to extract 3dmodel.model from the ZIP
    std::string modelXml = extractModelXML(path);
    if (modelXml.empty()) {
        result.error = "3MF archive missing required model file (3D/3dmodel.model). "
                       "Archive may be corrupt or use unsupported compression.";
        return result;
    }

    return parseModelXML(modelXml);
}

LoadResult ThreeMFLoader::loadFromBuffer(const ByteBuffer& data) {
    if (data.empty()) {
        return LoadResult{nullptr, "Empty buffer"};
    }

    if (data.size() < 22) {
        return LoadResult{nullptr, "3MF archive is corrupt or truncated (too small for ZIP)"};
    }

    std::string modelXml = extractModelXMLFromBuffer(data);
    if (modelXml.empty()) {
        return LoadResult{nullptr,
                          "3MF archive missing required model file (3D/3dmodel.model). "
                          "Archive may be corrupt or use unsupported compression."};
    }

    return parseModelXML(modelXml);
}

bool ThreeMFLoader::supports(const std::string& extension) const {
    std::string lower = extension;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower == "3mf";
}

std::vector<std::string> ThreeMFLoader::extensions() const {
    return {"3mf"};
}

std::string ThreeMFLoader::extractModelXML(const Path& zipPath) {
    std::string content;

    // Try common paths for the model file in 3MF archives
    const std::vector<std::string> modelPaths = {"3D/3dmodel.model",
                                                 "3dmodel.model",
                                                 "3D/model.model"};

    for (const auto& modelPath : modelPaths) {
        if (extractFromZip(zipPath, modelPath, content)) {
            return content;
        }
    }

    return "";
}

std::string ThreeMFLoader::extractModelXMLFromBuffer(const ByteBuffer& data) {
    std::string content;

    const std::vector<std::string> modelPaths = {"3D/3dmodel.model",
                                                 "3dmodel.model",
                                                 "3D/model.model"};

    for (const auto& modelPath : modelPaths) {
        if (extractFromZipBuffer(data, modelPath, content)) {
            return content;
        }
    }

    return "";
}

LoadResult ThreeMFLoader::parseModelXML(const std::string& xmlContent) {
    LoadResult result;
    result.mesh = std::make_shared<Mesh>();

    // Find vertices block
    size_t vertStart = xmlContent.find("<vertices>");
    size_t vertEnd = xmlContent.find("</vertices>");
    if (vertStart == std::string::npos || vertEnd == std::string::npos) {
        result.error = "No vertices found in 3MF model";
        result.mesh.reset();
        return result;
    }

    std::string verticesBlock = xmlContent.substr(vertStart + 10, vertEnd - vertStart - 10);

    std::vector<Vec3> vertices;
    if (!parseVertices(verticesBlock, vertices)) {
        result.error = "Failed to parse vertices";
        result.mesh.reset();
        return result;
    }

    // Find triangles block
    size_t triStart = xmlContent.find("<triangles>");
    size_t triEnd = xmlContent.find("</triangles>");
    if (triStart == std::string::npos || triEnd == std::string::npos) {
        result.error = "No triangles found in 3MF model";
        result.mesh.reset();
        return result;
    }

    std::string trianglesBlock = xmlContent.substr(triStart + 11, triEnd - triStart - 11);

    if (!parseTriangles(trianglesBlock, vertices, *result.mesh)) {
        result.error = "Failed to parse triangles";
        result.mesh.reset();
        return result;
    }

    // Validate output mesh
    if (result.mesh->triangleCount() == 0) {
        result.error = "3MF model contains no geometry";
        result.mesh.reset();
        return result;
    }

    result.mesh->recalculateBounds();

    log::infof("3MF",
               "Loaded: %u vertices, %u triangles",
               result.mesh->vertexCount(),
               result.mesh->triangleCount());

    // Validate mesh integrity (only fatal issues: NaN/Inf, OOB indices)
    if (!result.mesh->validateGeometry()) {
        result.error =
            "Mesh validation failed: invalid NaN/Inf vertex positions or out-of-bounds indices";
        result.mesh.reset();
        return result;
    }
    result.mesh->validate(); // Log warnings for degenerate triangles (non-fatal)

    return result;
}

bool ThreeMFLoader::parseVertices(const std::string& verticesBlock,
                                  std::vector<Vec3>& outVertices) {
    // Count vertex tags to reserve capacity and avoid repeated reallocations
    {
        size_t count = 0;
        size_t searchPos = 0;
        while ((searchPos = verticesBlock.find("<vertex", searchPos)) != std::string::npos) {
            ++count;
            searchPos += 7; // length of "<vertex"
        }
        outVertices.reserve(count);
    }

    // Parse <vertex x="..." y="..." z="..." /> tags
    size_t pos = 0;
    while ((pos = verticesBlock.find("<vertex", pos)) != std::string::npos) {
        size_t tagEnd = verticesBlock.find("/>", pos);
        if (tagEnd == std::string::npos) {
            tagEnd = verticesBlock.find(">", pos);
        }
        if (tagEnd == std::string::npos)
            break;

        std::string tag = verticesBlock.substr(pos, tagEnd - pos);

        std::string xStr = getXmlAttribute(tag, "x");
        std::string yStr = getXmlAttribute(tag, "y");
        std::string zStr = getXmlAttribute(tag, "z");

        if (!xStr.empty() && !yStr.empty() && !zStr.empty()) {
            try {
                Vec3 v;
                v.x = std::stof(xStr);
                v.y = std::stof(yStr);
                v.z = std::stof(zStr);

                // Validate vertex for NaN/Inf
                if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
                    log::warningf("3MF", "Invalid vertex with NaN/Inf coordinates, skipping");
                    pos = tagEnd + 1;
                    continue;
                }

                outVertices.push_back(v);
            } catch (const std::exception& e) {
                log::warningf("3MF", "Failed to parse vertex coordinates: %s", e.what());
            }
        }

        pos = tagEnd + 1;
    }

    return !outVertices.empty();
}

bool ThreeMFLoader::parseTriangles(const std::string& trianglesBlock,
                                   const std::vector<Vec3>& vertices,
                                   Mesh& outMesh) {
    // Count triangle tags to reserve capacity and avoid repeated reallocations.
    // Each triangle creates 3 vertices (flat shading, no deduplication) and 3 indices.
    {
        size_t count = 0;
        size_t searchPos = 0;
        while ((searchPos = trianglesBlock.find("<triangle", searchPos)) != std::string::npos) {
            ++count;
            searchPos += 9; // length of "<triangle"
        }
        outMesh.reserve(static_cast<u32>(count * 3), static_cast<u32>(count * 3));
    }

    // Parse <triangle v1="..." v2="..." v3="..." /> tags
    size_t pos = 0;
    while ((pos = trianglesBlock.find("<triangle", pos)) != std::string::npos) {
        size_t tagEnd = trianglesBlock.find("/>", pos);
        if (tagEnd == std::string::npos) {
            tagEnd = trianglesBlock.find(">", pos);
        }
        if (tagEnd == std::string::npos)
            break;

        std::string tag = trianglesBlock.substr(pos, tagEnd - pos);

        std::string v1Str = getXmlAttribute(tag, "v1");
        std::string v2Str = getXmlAttribute(tag, "v2");
        std::string v3Str = getXmlAttribute(tag, "v3");

        if (!v1Str.empty() && !v2Str.empty() && !v3Str.empty()) {
            unsigned long v1Long = std::stoul(v1Str);
            unsigned long v2Long = std::stoul(v2Str);
            unsigned long v3Long = std::stoul(v3Str);
            constexpr auto maxIndex = static_cast<unsigned long>(std::numeric_limits<uint32_t>::max());
            if (v1Long > maxIndex || v2Long > maxIndex || v3Long > maxIndex) {
                pos = tagEnd + 1;
                continue;
            }

            uint32_t v1 = static_cast<uint32_t>(v1Long);
            uint32_t v2 = static_cast<uint32_t>(v2Long);
            uint32_t v3 = static_cast<uint32_t>(v3Long);

            if (v1 < vertices.size() && v2 < vertices.size() && v3 < vertices.size()) {
                // Calculate face normal
                Vec3 p1 = vertices[static_cast<size_t>(v1)];
                Vec3 p2 = vertices[static_cast<size_t>(v2)];
                Vec3 p3 = vertices[static_cast<size_t>(v3)];

                Vec3 edge1 = p2 - p1;
                Vec3 edge2 = p3 - p1;
                Vec3 normal = glm::normalize(glm::cross(edge1, edge2));

                // Add vertices with normals
                uint32_t baseIdx = static_cast<uint32_t>(outMesh.vertices().size());

                Vertex vert1, vert2, vert3;
                vert1.position = p1;
                vert1.normal = normal;
                vert2.position = p2;
                vert2.normal = normal;
                vert3.position = p3;
                vert3.normal = normal;

                outMesh.addVertex(vert1);
                outMesh.addVertex(vert2);
                outMesh.addVertex(vert3);

                outMesh.addTriangle(baseIdx, baseIdx + 1, baseIdx + 2);
            }
        }

        pos = tagEnd + 1;
    }

    return outMesh.triangleCount() > 0;
}

} // namespace dw

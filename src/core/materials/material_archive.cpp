#include "material_archive.h"

#include <cstring>
#include <sstream>

#include <miniz.h>
#include <nlohmann/json.hpp>

#include "../utils/log.h"

namespace dw {

namespace {

constexpr const char* kTextureEntry = "texture.png";
constexpr const char* kMetadataEntry = "metadata.json";
constexpr mz_uint kCompressionLevel = static_cast<mz_uint>(MZ_DEFAULT_COMPRESSION);

} // namespace

// ---------------------------------------------------------------------------
// JSON serialization
// ---------------------------------------------------------------------------

std::string MaterialArchive::metadataToJson(const MaterialRecord& record) {
    nlohmann::json j;
    j["name"] = record.name;
    j["category"] = materialCategoryToString(record.category);
    j["janka_hardness"] = record.jankaHardness;
    j["feed_rate"] = record.feedRate;
    j["spindle_speed"] = record.spindleSpeed;
    j["depth_of_cut"] = record.depthOfCut;
    j["grain_direction_deg"] = record.grainDirectionDeg;
    j["texture_file"] = kTextureEntry;
    j["version"] = 1;
    return j.dump(2);
}

std::optional<MaterialRecord> MaterialArchive::jsonToMaterial(const std::string& json) {
    try {
        auto j = nlohmann::json::parse(json);
        MaterialRecord record;
        record.name = j.value("name", std::string{});
        record.category = stringToMaterialCategory(j.value("category", std::string{"hardwood"}));
        record.jankaHardness = j.value("janka_hardness", 0.0f);
        record.feedRate = j.value("feed_rate", 0.0f);
        record.spindleSpeed = j.value("spindle_speed", 0.0f);
        record.depthOfCut = j.value("depth_of_cut", 0.0f);
        record.grainDirectionDeg = j.value("grain_direction_deg", 0.0f);
        return record;
    } catch (const nlohmann::json::exception& e) {
        log::errorf("MaterialArchive", "JSON parse error: %s", e.what());
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// create()
// ---------------------------------------------------------------------------

ArchiveResult MaterialArchive::create(const std::string& archivePath,
                                      const std::string& texturePath,
                                      const MaterialRecord& record) {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);

    if (!mz_zip_writer_init_file(&zip, archivePath.c_str(), 0)) {
        return ArchiveResult::fail("Failed to create archive file: " + archivePath);
    }

    // Add texture.png from file (optional — default materials have no texture)
    std::vector<std::string> archivedFiles;
    if (!texturePath.empty()) {
        if (!mz_zip_writer_add_file(
                &zip, kTextureEntry, texturePath.c_str(), nullptr, 0, kCompressionLevel)) {
            mz_zip_writer_end(&zip);
            return ArchiveResult::fail("Failed to add texture to archive: " + texturePath);
        }
        archivedFiles.push_back(kTextureEntry);
    }

    // Serialize metadata to JSON and add it
    std::string metaJson = metadataToJson(record);
    if (!mz_zip_writer_add_mem(
            &zip, kMetadataEntry, metaJson.c_str(), metaJson.size(), kCompressionLevel)) {
        mz_zip_writer_end(&zip);
        return ArchiveResult::fail("Failed to add metadata.json to archive");
    }

    if (!mz_zip_writer_finalize_archive(&zip)) {
        mz_zip_writer_end(&zip);
        return ArchiveResult::fail("Failed to finalize archive: " + archivePath);
    }

    mz_zip_writer_end(&zip);

    archivedFiles.push_back(kMetadataEntry);
    log::infof(
        "MaterialArchive", "Created: %s (%zu entries)", archivePath.c_str(), archivedFiles.size());
    return ArchiveResult::ok(std::move(archivedFiles));
}

// ---------------------------------------------------------------------------
// load()
// ---------------------------------------------------------------------------

std::optional<MaterialData> MaterialArchive::load(const std::string& archivePath) {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);

    if (!mz_zip_reader_init_file(&zip, archivePath.c_str(), 0)) {
        log::errorf("MaterialArchive", "Failed to open archive: %s", archivePath.c_str());
        return std::nullopt;
    }

    // Extract texture.png to heap (optional — metadata-only archives may omit it)
    size_t textureSize = 0;
    void* textureBuf = mz_zip_reader_extract_file_to_heap(&zip, kTextureEntry, &textureSize, 0);

    // Extract metadata.json to heap (required)
    size_t metaSize = 0;
    void* metaBuf = mz_zip_reader_extract_file_to_heap(&zip, kMetadataEntry, &metaSize, 0);
    if (!metaBuf) {
        log::errorf("MaterialArchive",
                    "metadata.json not found in archive: %s",
                    archivePath.c_str());
        if (textureBuf) {
            mz_free(textureBuf);
        }
        mz_zip_reader_end(&zip);
        return std::nullopt;
    }

    mz_zip_reader_end(&zip);

    // Parse metadata JSON
    std::string metaJson(static_cast<char*>(metaBuf), metaSize);
    mz_free(metaBuf);

    auto record = jsonToMaterial(metaJson);
    if (!record) {
        if (textureBuf) {
            mz_free(textureBuf);
        }
        return std::nullopt;
    }

    // Build result
    MaterialData data;
    data.metadata = std::move(*record);

    if (textureBuf) {
        data.textureData.assign(static_cast<uint8_t*>(textureBuf),
                                static_cast<uint8_t*>(textureBuf) + textureSize);
        mz_free(textureBuf);
    }

    return data;
}

// ---------------------------------------------------------------------------
// list()
// ---------------------------------------------------------------------------

std::vector<ArchiveEntry> MaterialArchive::list(const std::string& archivePath) {
    std::vector<ArchiveEntry> entries;

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);

    if (!mz_zip_reader_init_file(&zip, archivePath.c_str(), 0)) {
        return entries;
    }

    mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    entries.reserve(numFiles);

    for (mz_uint i = 0; i < numFiles; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            continue;
        }

        ArchiveEntry entry;
        entry.path = stat.m_filename;
        entry.uncompressedSize = stat.m_uncomp_size;
        entry.compressedSize = stat.m_comp_size;
        entry.isDirectory = stat.m_is_directory != 0;
        entries.push_back(std::move(entry));
    }

    mz_zip_reader_end(&zip);
    return entries;
}

// ---------------------------------------------------------------------------
// isValidArchive()
// ---------------------------------------------------------------------------

bool MaterialArchive::isValidArchive(const std::string& archivePath) {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);

    if (!mz_zip_reader_init_file(&zip, archivePath.c_str(), 0)) {
        return false;
    }

    // Must contain metadata.json
    int idx = mz_zip_reader_locate_file(&zip, kMetadataEntry, nullptr, 0);
    mz_zip_reader_end(&zip);

    return idx >= 0;
}

} // namespace dw

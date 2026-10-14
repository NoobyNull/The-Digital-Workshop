#include "material_manager.h"

#include <set>

#include "../config/config.h"
#include "../paths/app_paths.h"
#include "../paths/path_resolver.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"
#include "default_materials.h"
#include "material_archive.h"

namespace dw {

MaterialManager::MaterialManager(Database& db) : m_db(db), m_repo(db) {}

// ---------------------------------------------------------------------------
// seedDefaults
// ---------------------------------------------------------------------------

void MaterialManager::seedDefaults() {
    i64 existing = m_repo.count();
    if (existing > 0) {
        log::debugf("MaterialManager",
                    "seedDefaults: %lld materials already present, skipping",
                    static_cast<long long>(existing));
        return;
    }

    // Build a set of default material names for fallback tracking
    auto defaults = getDefaultMaterials();
    std::set<std::string> seededNames;

    int seededWithTexture = 0;
    int seededBare = 0;

    // Phase 1: Load bundled .dwmat files in-place (no copy to managed dir)
    Path bundledDir = paths::getBundledMaterialsDir();
    if (file::isDirectory(bundledDir)) {
        auto dwmatFiles = file::listFiles(bundledDir, "dwmat");
        for (const auto& dwmatPath : dwmatFiles) {
            auto data = MaterialArchive::load(dwmatPath.string());
            if (!data) {
                log::warningf("MaterialManager",
                              "seedDefaults: failed to load bundled archive: %s",
                              dwmatPath.string().c_str());
                continue;
            }
            MaterialRecord record = data->metadata;
            record.archivePath = dwmatPath.filename();
            record.isBundled = true;
            auto id = m_repo.insert(record);
            if (id) {
                seededNames.insert(record.name);
                ++seededWithTexture;
            }
        }
    }

    // Phase 2: Fallback — insert bare records for any defaults not found as .dwmat
    if (seededNames.size() < defaults.size()) {
        Transaction txn(m_db);
        for (const auto& mat : defaults) {
            if (seededNames.count(mat.name) > 0) {
                continue; // Already imported from .dwmat
            }
            MaterialRecord bundledMat = mat;
            bundledMat.isBundled = true;
            auto id = m_repo.insert(bundledMat);
            if (id) {
                ++seededBare;
            } else {
                log::warningf("MaterialManager",
                              "seedDefaults: failed to insert '%s'",
                              mat.name.c_str());
            }
        }
        if (!txn.commit()) {
            log::error("MaterialManager", "seedDefaults: transaction commit failed");
            return;
        }
    }

    log::infof("MaterialManager",
               "Seeded %d materials (%d with textures)",
               seededWithTexture + seededBare,
               seededWithTexture);
}

// ---------------------------------------------------------------------------
// importMaterial
// ---------------------------------------------------------------------------

std::optional<i64> MaterialManager::importMaterial(const Path& dwmatPath) {
    if (!file::isFile(dwmatPath)) {
        log::errorf("MaterialManager",
                    "importMaterial: source does not exist: %s",
                    dwmatPath.string().c_str());
        return std::nullopt;
    }

    // Validate archive before copying
    if (!MaterialArchive::isValidArchive(dwmatPath.string())) {
        log::errorf("MaterialManager",
                    "importMaterial: not a valid .dwmat archive: %s",
                    dwmatPath.string().c_str());
        return std::nullopt;
    }

    // Load metadata from source
    auto data = MaterialArchive::load(dwmatPath.string());
    if (!data) {
        log::errorf("MaterialManager",
                    "importMaterial: failed to load metadata from: %s",
                    dwmatPath.string().c_str());
        return std::nullopt;
    }

    // Copy to user materials directory using original filename.
    // This overwrites any existing file, letting user imports take priority
    // over bundled defaults with the same name.
    Path materialsDir = Config::instance().getMaterialsDir();
    static_cast<void>(file::createDirectories(materialsDir));
    Path destPath = materialsDir / dwmatPath.filename().string();
    if (!file::copy(dwmatPath, destPath)) {
        log::errorf("MaterialManager",
                    "importMaterial: failed to copy %s -> %s",
                    dwmatPath.string().c_str(),
                    destPath.string().c_str());
        return std::nullopt;
    }

    // Build record — store just the filename (resolved via user dir / bundled dir)
    MaterialRecord record = data->metadata;
    record.archivePath = destPath.filename();

    // If a material with the same name already exists, update it
    auto existing = m_repo.findByExactName(record.name);
    if (existing) {
        record.id = existing->id;
        if (!m_repo.update(record)) {
            log::errorf("MaterialManager",
                        "importMaterial: database update failed for: %s",
                        record.name.c_str());
            return std::nullopt;
        }
        return record.id;
    }

    // Otherwise insert new
    auto id = m_repo.insert(record);
    if (!id) {
        log::errorf("MaterialManager",
                    "importMaterial: database insert failed for: %s",
                    record.name.c_str());
        static_cast<void>(file::remove(destPath));
        return std::nullopt;
    }

    log::infof("MaterialManager",
               "Imported material '%s' (id=%lld) from %s",
               record.name.c_str(),
               static_cast<long long>(*id),
               dwmatPath.string().c_str());
    return id;
}

// ---------------------------------------------------------------------------
// exportMaterial
// ---------------------------------------------------------------------------

bool MaterialManager::exportMaterial(i64 materialId, const Path& outputPath) {
    auto matOpt = m_repo.findById(materialId);
    if (!matOpt) {
        log::errorf("MaterialManager",
                    "exportMaterial: material %lld not found",
                    static_cast<long long>(materialId));
        return false;
    }

    const MaterialRecord& mat = *matOpt;

    if (!mat.archivePath.empty() && file::isFile(mat.archivePath)) {
        // Material has a managed archive — just copy it to the output path
        if (!file::copy(mat.archivePath, outputPath)) {
            log::errorf("MaterialManager",
                        "exportMaterial: copy failed %s -> %s",
                        mat.archivePath.string().c_str(),
                        outputPath.string().c_str());
            return false;
        }
        log::infof("MaterialManager",
                   "Exported material '%s' to %s",
                   mat.name.c_str(),
                   outputPath.string().c_str());
        return true;
    }

    // Default species — no texture file, create a metadata-only .dwmat.
    // Pass empty texture path to create archive without texture entry.
    auto result = MaterialArchive::create(outputPath.string(), std::string{}, mat);
    if (!result.success) {
        log::errorf("MaterialManager",
                    "exportMaterial: create archive failed: %s",
                    result.error.c_str());
        return false;
    }

    log::infof("MaterialManager",
               "Exported default material '%s' as metadata-only archive to %s",
               mat.name.c_str(),
               outputPath.string().c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Read operations
// ---------------------------------------------------------------------------

std::vector<MaterialRecord> MaterialManager::getAllMaterials() {
    return m_repo.findAll();
}

std::vector<MaterialRecord> MaterialManager::getMaterialsByCategory(MaterialCategory category) {
    return m_repo.findByCategory(category);
}

std::optional<MaterialRecord> MaterialManager::getMaterial(i64 id) {
    return m_repo.findById(id);
}

// ---------------------------------------------------------------------------
// Write operations
// ---------------------------------------------------------------------------

std::optional<i64> MaterialManager::addMaterial(const MaterialRecord& record) {
    return m_repo.insert(record);
}

bool MaterialManager::updateMaterial(const MaterialRecord& record) {
    return m_repo.update(record);
}

bool MaterialManager::removeMaterial(i64 id) {
    auto matOpt = m_repo.findById(id);
    if (!matOpt) {
        return false;
    }

    // Bundled materials are hidden, not deleted
    if (matOpt->isBundled) {
        return setMaterialHidden(id, true);
    }

    // Delete the user's copy if it exists — never touch the bundled directory
    if (!matOpt->archivePath.empty()) {
        Path userCopy = Config::instance().getMaterialsDir() / matOpt->archivePath;
        if (file::isFile(userCopy)) {
            if (!file::remove(userCopy)) {
                log::warningf("MaterialManager",
                              "removeMaterial: could not delete archive file: %s",
                              userCopy.string().c_str());
            }
        }
    }

    return m_repo.remove(id);
}

bool MaterialManager::setMaterialHidden(i64 id, bool hidden) {
    auto matOpt = m_repo.findById(id);
    if (!matOpt) {
        return false;
    }
    matOpt->isHidden = hidden;
    return m_repo.update(*matOpt);
}

// ---------------------------------------------------------------------------
// Material-to-model assignment
// ---------------------------------------------------------------------------

bool MaterialManager::assignMaterialToModel(i64 materialId, i64 modelId) {
    // Verify the material exists
    if (!m_repo.findById(materialId)) {
        log::errorf("MaterialManager",
                    "assignMaterialToModel: material %lld not found",
                    static_cast<long long>(materialId));
        return false;
    }

    auto stmt = m_db.prepare("UPDATE models SET material_id = ? WHERE id = ?");
    if (!stmt.isValid()) {
        return false;
    }
    if (!stmt.bindInt(1, materialId) || !stmt.bindInt(2, modelId)) {
        return false;
    }

    if (!stmt.execute()) {
        log::errorf("MaterialManager",
                    "assignMaterialToModel: UPDATE failed for model %lld",
                    static_cast<long long>(modelId));
        return false;
    }

    return m_db.changesCount() > 0;
}

bool MaterialManager::clearMaterialAssignment(i64 modelId) {
    auto stmt = m_db.prepare("UPDATE models SET material_id = NULL WHERE id = ?");
    if (!stmt.isValid()) {
        return false;
    }
    if (!stmt.bindInt(1, modelId)) {
        return false;
    }
    return stmt.execute() && m_db.changesCount() > 0;
}

std::optional<MaterialRecord> MaterialManager::getModelMaterial(i64 modelId) {
    auto stmt = m_db.prepare("SELECT material_id FROM models WHERE id = ?");
    if (!stmt.isValid()) {
        return std::nullopt;
    }
    if (!stmt.bindInt(1, modelId)) {
        return std::nullopt;
    }
    if (!stmt.step()) {
        return std::nullopt; // Model not found
    }
    if (stmt.isNull(0)) {
        return std::nullopt; // No material assigned
    }

    i64 materialId = stmt.getInt(0);
    return m_repo.findById(materialId);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

Path MaterialManager::uniqueArchivePath(const std::string& originalFilename) const {
    Path materialsDir = Config::instance().getMaterialsDir();

    // Ensure directory exists
    static_cast<void>(file::createDirectories(materialsDir));

    Path candidate = materialsDir / originalFilename;
    if (!file::exists(candidate)) {
        return candidate;
    }

    // Extract stem and extension to build collision-safe names
    Path p(originalFilename);
    std::string stem = file::getStem(p);
    std::string ext = "." + file::getExtension(p);
    if (ext == ".") {
        ext = ".dwmat";
    }

    for (int i = 1; i < 1000; ++i) {
        std::string newName = stem + "_" + std::to_string(i) + ext;
        candidate = materialsDir / newName;
        if (!file::exists(candidate)) {
            return candidate;
        }
    }

    // Fallback (should never happen in practice)
    return materialsDir / (originalFilename + ".dwmat");
}

} // namespace dw

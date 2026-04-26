#include "library_manager.h"

#include "../../render/thumbnail_generator.h"
#include "../graph/graph_manager.h"
#include "../loaders/loader_factory.h"
#include "../mesh/hash.h"
#include "../paths/app_paths.h"
#include "../paths/path_resolver.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"

namespace dw {

LibraryManager::LibraryManager(Database& db) : m_db(db), m_modelRepo(db), m_gcodeRepo(db) {}

ImportResult LibraryManager::importModel(const Path& sourcePath) {
    ImportResult result;

    // Check file exists
    if (!file::exists(sourcePath)) {
        result.error = "File does not exist: " + sourcePath.string();
        log::error("Library", result.error);
        return result;
    }

    // Compute hash for deduplication
    std::string fileHash = computeFileHash(sourcePath);
    if (fileHash.empty()) {
        result.error = "Failed to compute file hash";
        log::error("Library", result.error);
        return result;
    }

    // Check for duplicate
    if (modelExists(fileHash)) {
        auto existing = getModelByHash(fileHash);
        if (existing) {
            result.isDuplicate = true;

            // Ask user what to do with duplicate
            if (m_duplicateHandler && !m_duplicateHandler(*existing)) {
                result.error = "Import cancelled: duplicate model";
                log::info("Library", "Import cancelled: duplicate model");
                return result;
            }

            // User wants to re-import (or no handler set)
            log::infof("Library", "Re-importing duplicate model: %s", existing->name.c_str());
            m_modelRepo.removeByHash(fileHash);
        }
    }

    // Load the mesh
    auto loadResult = LoaderFactory::load(sourcePath);
    if (!loadResult) {
        result.error = "Failed to load model: " + loadResult.error;
        log::error("Library", result.error);
        return result;
    }

    MeshPtr mesh = loadResult.mesh;

    // Create model record
    ModelRecord record;
    record.hash = fileHash;
    record.name = file::getStem(sourcePath);
    record.filePath = PathResolver::makeStorable(sourcePath, PathCategory::Support);
    record.fileFormat = file::getExtension(sourcePath);

    auto fileSize = file::getFileSize(sourcePath);
    record.fileSize = fileSize ? *fileSize : 0;

    record.vertexCount = mesh->vertexCount();
    record.triangleCount = mesh->triangleCount();
    record.boundsMin = mesh->bounds().min;
    record.boundsMax = mesh->bounds().max;

    // Insert into database
    auto modelId = m_modelRepo.insert(record);
    if (!modelId) {
        result.error = "Failed to save model to database";
        log::error("Library", result.error);
        return result;
    }

    // Generate thumbnail (best effort)
    generateThumbnail(*modelId, *mesh);

    // Dual-write: create graph node (non-fatal)
    if (m_graphManager && m_graphManager->isAvailable()) {
        if (!m_graphManager->addModelNode(*modelId, record.name, record.hash)) {
            log::warningf("Library",
                          "Failed to create graph node for model %lld",
                          static_cast<long long>(*modelId));
        }
    }

    result.success = true;
    result.modelId = *modelId;

    log::infof("Library",
               "Imported model: %s (ID: %lld, %u triangles)",
               record.name.c_str(),
               static_cast<long long>(*modelId),
               record.triangleCount);

    return result;
}

std::vector<ModelRecord> LibraryManager::getAllModels() {
    return m_modelRepo.findAll();
}

std::vector<ModelRecord> LibraryManager::searchModels(const std::string& query) {
    return m_modelRepo.findByName(query);
}

std::vector<ModelRecord> LibraryManager::filterByTagStatus(int status) {
    return m_modelRepo.findByTagStatus(status);
}

std::optional<ModelRecord> LibraryManager::getModel(i64 modelId) {
    return m_modelRepo.findById(modelId);
}

std::optional<ModelRecord> LibraryManager::getModelByHash(const std::string& hash) {
    return m_modelRepo.findByHash(hash);
}

MeshPtr LibraryManager::loadMesh(i64 modelId) {
    auto record = getModel(modelId);
    if (!record) {
        return nullptr;
    }
    return loadMesh(*record);
}

MeshPtr LibraryManager::loadMesh(const ModelRecord& record) {
    auto loadResult = LoaderFactory::load(PathResolver::resolve(record.filePath, PathCategory::Support));
    if (!loadResult) {
        log::errorf("Library", "Failed to load mesh: %s", loadResult.error.c_str());
        return nullptr;
    }

    loadResult.mesh->setName(record.name);
    return loadResult.mesh;
}

bool LibraryManager::updateModel(const ModelRecord& record) {
    return m_modelRepo.update(record);
}

bool LibraryManager::updateTags(i64 modelId, const std::vector<std::string>& tags) {
    return m_modelRepo.updateTags(modelId, tags);
}

bool LibraryManager::updateTagStatus(i64 modelId, int status) {
    return m_modelRepo.updateTagStatus(modelId, status);
}

bool LibraryManager::removeModel(i64 modelId) {
    auto record = getModel(modelId);
    if (!record) {
        return false;
    }

    // Remove thumbnail if exists (best-effort cleanup)
    if (!record->thumbnailPath.empty() && file::exists(record->thumbnailPath)) {
        (void)file::remove(record->thumbnailPath);
    }

    // Dual-write: remove graph node (non-fatal)
    if (m_graphManager && m_graphManager->isAvailable()) {
        m_graphManager->removeModelNode(modelId);
    }

    return m_modelRepo.remove(modelId);
}

i64 LibraryManager::modelCount() {
    return m_modelRepo.count();
}

bool LibraryManager::modelExists(const std::string& hash) {
    return m_modelRepo.exists(hash);
}

std::string LibraryManager::computeFileHash(const Path& path) {
    return hash::computeFile(path);
}

bool LibraryManager::generateThumbnail(i64 modelId,
                                       const Mesh& mesh,
                                       const Texture* materialTexture,
                                       float cameraPitch,
                                       float cameraYaw) {
    if (!m_thumbnailGen) {
        log::warning("Library", "Thumbnail generation skipped - no generator available");
        return false;
    }

    // Ensure thumbnail directory exists
    Path thumbnailDir = paths::getThumbnailDir();
    if (!file::exists(thumbnailDir)) {
        if (!file::createDirectories(thumbnailDir)) {
            log::warning("Library", "Failed to create thumbnail directory");
            return false;
        }
    }

    Path thumbnailPath = thumbnailDir / (std::to_string(modelId) + ".tga");

    ThumbnailSettings settings;
    settings.materialTexture = materialTexture;
    settings.cameraPitch = cameraPitch;
    settings.cameraYaw = cameraYaw;

    if (!m_thumbnailGen->generate(mesh, thumbnailPath, settings)) {
        log::warningf("Library",
                      "Failed to generate thumbnail for model %lld",
                      static_cast<long long>(modelId));
        return false;
    }

    // Update database with thumbnail path
    m_modelRepo.updateThumbnail(modelId, thumbnailPath);

    log::infof("Library", "Generated thumbnail: %s", thumbnailPath.string().c_str());
    return true;
}

// --- G-code operations ---

std::vector<GCodeRecord> LibraryManager::getAllGCodeFiles() {
    return m_gcodeRepo.findAll();
}

std::vector<GCodeRecord> LibraryManager::searchGCodeFiles(const std::string& query) {
    return m_gcodeRepo.findByName(query);
}

std::optional<GCodeRecord> LibraryManager::getGCodeFile(i64 id) {
    return m_gcodeRepo.findById(id);
}

bool LibraryManager::deleteGCodeFile(i64 id) {
    auto record = getGCodeFile(id);
    if (!record) {
        return false;
    }

    // Remove thumbnail if exists (best-effort cleanup)
    if (!record->thumbnailPath.empty() && file::exists(record->thumbnailPath)) {
        (void)file::remove(record->thumbnailPath);
    }

    return m_gcodeRepo.remove(id);
}

// --- Hierarchy operations ---

std::optional<i64> LibraryManager::createOperationGroup(i64 modelId,
                                                        const std::string& name,
                                                        int sortOrder) {
    return m_gcodeRepo.createGroup(modelId, name, sortOrder);
}

std::vector<OperationGroup> LibraryManager::getOperationGroups(i64 modelId) {
    return m_gcodeRepo.getGroups(modelId);
}

bool LibraryManager::addGCodeToGroup(i64 groupId, i64 gcodeId, int sortOrder) {
    return m_gcodeRepo.addToGroup(groupId, gcodeId, sortOrder);
}

bool LibraryManager::removeGCodeFromGroup(i64 groupId, i64 gcodeId) {
    return m_gcodeRepo.removeFromGroup(groupId, gcodeId);
}

std::vector<GCodeRecord> LibraryManager::getGroupGCodeFiles(i64 groupId) {
    return m_gcodeRepo.getGroupMembers(groupId);
}

bool LibraryManager::deleteOperationGroup(i64 groupId) {
    return m_gcodeRepo.deleteGroup(groupId);
}

// --- Template operations ---

std::vector<GCodeTemplate> LibraryManager::getTemplates() {
    return m_gcodeRepo.getTemplates();
}

bool LibraryManager::applyTemplate(i64 modelId, const std::string& templateName) {
    return m_gcodeRepo.applyTemplate(modelId, templateName);
}

// --- Auto-detect ---

std::optional<i64> LibraryManager::autoDetectModelMatch(const std::string& gcodeFilename) {
    // Strip extension
    std::string baseName = gcodeFilename;
    size_t dotPos = baseName.find_last_of('.');
    if (dotPos != std::string::npos) {
        baseName = baseName.substr(0, dotPos);
    }

    // Strip common G-code suffixes
    const std::vector<std::string> suffixes = {"_roughing",
                                               "_finishing",
                                               "_profile",
                                               "_profiling",
                                               "_drill",
                                               "_drilling",
                                               "_contour",
                                               "_contouring",
                                               "_pocket",
                                               "_pocketing",
                                               "_trace",
                                               "_tracing",
                                               "_engrave",
                                               "_engraving",
                                               "_cut",
                                               "_cutting",
                                               "_mill",
                                               "_milling"};

    for (const auto& suffix : suffixes) {
        if (baseName.size() > suffix.size()) {
            size_t pos = baseName.size() - suffix.size();
            if (baseName.substr(pos) == suffix) {
                baseName = baseName.substr(0, pos);
                break;
            }
        }
    }

    // Search for models with this base name
    auto matches = m_modelRepo.findByName(baseName);

    // Only return confident single match
    if (matches.size() == 1) {
        return matches[0].id;
    }

    // Zero or multiple matches: ambiguous, return nullopt
    return std::nullopt;
}

// --- Category management (dual-write to SQLite + graph) ---

bool LibraryManager::assignCategory(i64 modelId, i64 categoryId) {
    // SQLite is source of truth
    bool ok = m_modelRepo.assignCategory(modelId, categoryId);
    if (!ok)
        return false;

    // Dual-write: graph edge (non-fatal)
    if (m_graphManager && m_graphManager->isAvailable()) {
        m_graphManager->addBelongsToEdge(modelId, categoryId);
    }
    return true;
}

bool LibraryManager::removeModelCategory(i64 modelId, i64 categoryId) {
    bool ok = m_modelRepo.removeCategory(modelId, categoryId);
    if (!ok)
        return false;

    // Dual-write: remove graph edge (non-fatal)
    if (m_graphManager && m_graphManager->isAvailable()) {
        m_graphManager->removeBelongsToEdge(modelId, categoryId);
    }
    return true;
}

std::optional<i64> LibraryManager::createCategory(const std::string& name,
                                                  std::optional<i64> parentId) {
    auto catId = m_modelRepo.createCategory(name, parentId);
    if (!catId)
        return std::nullopt;

    // Dual-write: create graph node (non-fatal)
    if (m_graphManager && m_graphManager->isAvailable()) {
        m_graphManager->addCategoryNode(*catId, name);
    }
    return catId;
}

bool LibraryManager::deleteCategory(i64 categoryId) {
    bool ok = m_modelRepo.deleteCategory(categoryId);
    if (!ok)
        return false;

    // Dual-write: remove graph node (non-fatal)
    if (m_graphManager && m_graphManager->isAvailable()) {
        m_graphManager->removeCategoryNode(categoryId);
    }
    return true;
}

std::vector<CategoryRecord> LibraryManager::getAllCategories() {
    return m_modelRepo.getAllCategories();
}

std::vector<CategoryRecord> LibraryManager::getRootCategories() {
    return m_modelRepo.getRootCategories();
}

std::vector<CategoryRecord> LibraryManager::getChildCategories(i64 parentId) {
    return m_modelRepo.getChildCategories(parentId);
}

std::vector<ModelRecord> LibraryManager::filterByCategory(i64 categoryId) {
    return m_modelRepo.findByCategory(categoryId);
}

// --- FTS5 search ---

std::vector<ModelRecord> LibraryManager::searchModelsFTS(const std::string& query) {
    return m_modelRepo.searchFTS(query);
}

// --- AI Descriptor management ---

bool LibraryManager::updateDescriptor(i64 modelId,
                                      const std::string& title,
                                      const std::string& description,
                                      const std::string& hover) {
    return m_modelRepo.updateDescriptor(modelId, title, description, hover);
}

// Split a category name on " & ", " and ", " / " into individual names, trimmed.
static std::vector<std::string> splitCompoundCategory(const std::string& name) {
    std::vector<std::string> parts;
    std::string remaining = name;

    // Try each delimiter in order
    for (const char* delim : {" & ", " and ", " / "}) {
        std::string delimStr(delim);
        std::vector<std::string> next;
        for (auto& part : (parts.empty() ? std::vector<std::string>{remaining} : parts)) {
            size_t pos = 0;
            size_t found;
            while ((found = part.find(delimStr, pos)) != std::string::npos) {
                next.push_back(part.substr(pos, found - pos));
                pos = found + delimStr.size();
            }
            next.push_back(part.substr(pos));
        }
        parts = std::move(next);
    }

    // Trim whitespace
    std::vector<std::string> result;
    for (auto& p : parts) {
        size_t start = p.find_first_not_of(" \t");
        size_t end = p.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos) {
            result.push_back(p.substr(start, end - start + 1));
        }
    }
    return result;
}

// --- Library maintenance ---

MaintenanceReport LibraryManager::runMaintenance() {
    MaintenanceReport report;

    // (a) Split compound categories
    {
        auto allCats = getAllCategories();
        for (const auto& cat : allCats) {
            auto parts = splitCompoundCategory(cat.name);
            if (parts.size() <= 1)
                continue;

            // Get models assigned to this compound category
            auto models = m_modelRepo.findByCategory(cat.id);

            // For each part, find-or-create a category with same parent
            for (const auto& partName : parts) {
                auto existingId = m_modelRepo.findCategoryByNameAndParent(partName, cat.parentId);
                i64 newCatId = 0;
                if (existingId) {
                    newCatId = *existingId;
                } else {
                    auto created = createCategory(partName, cat.parentId);
                    if (!created)
                        continue;
                    newCatId = *created;
                }

                // Reassign each model to the new category
                for (const auto& model : models) {
                    assignCategory(model.id, newCatId);
                }
            }

            // Delete the compound category
            deleteCategory(cat.id);
            report.categoriesSplit++;
        }
    }

    // (b) Delete empty categories (leaf-first via recursive approach)
    {
        bool deleted = true;
        while (deleted) {
            deleted = false;
            auto allCats = getAllCategories();
            for (const auto& cat : allCats) {
                // Check if this category has children
                auto children = m_modelRepo.getChildCategories(cat.id);
                if (!children.empty())
                    continue;

                // Check if any models are directly assigned
                auto models = m_modelRepo.findByCategory(cat.id);
                if (!models.empty())
                    continue;

                // Leaf with no models — delete it
                deleteCategory(cat.id);
                report.categoriesRemoved++;
                deleted = true;
            }
        }
    }

    // (c) Deduplicate tags
    {
        auto allModels = getAllModels();
        for (const auto& model : allModels) {
            if (model.tags.empty())
                continue;

            std::vector<std::string> unique;
            for (const auto& tag : model.tags) {
                bool found = false;
                for (const auto& u : unique) {
                    if (u == tag) {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    unique.push_back(tag);
            }

            if (unique.size() < model.tags.size()) {
                updateTags(model.id, unique);
                report.tagsDeduped++;
            }
        }
    }

    // (d) Verify thumbnail paths
    {
        auto allModels = getAllModels();
        for (const auto& model : allModels) {
            if (model.thumbnailPath.empty())
                continue;
            if (!file::exists(model.thumbnailPath)) {
                m_modelRepo.updateThumbnail(model.id, Path{});
                report.thumbnailsCleared++;
            }
        }
    }

    // (e) Rebuild FTS index
    {
        auto stmt = m_db.prepare("INSERT INTO models_fts(models_fts) VALUES('rebuild')");
        if (stmt.isValid() && stmt.execute()) {
            report.ftsRebuilt = 1;
        }
    }

    return report;
}

bool LibraryManager::resolveAndAssignCategories(i64 modelId,
                                                const std::vector<std::string>& categoryChain) {
    if (categoryChain.empty()) {
        return true;
    }

    // Expand any compound names (e.g. "Art & Decor" -> ["Art", "Decor"])
    // at each level, creating parallel chains.
    // Example: ["Art & Design", "Figurine"] produces:
    //   Art > Figurine  (assigned)
    //   Design > Figurine  (assigned)

    // Build list of expanded levels
    std::vector<std::vector<std::string>> levels;
    for (const auto& catName : categoryChain) {
        levels.push_back(splitCompoundCategory(catName));
    }

    // Resolve a single chain and assign the leaf
    auto resolveChain = [&](const std::vector<std::string>& chain) -> bool {
        std::optional<i64> parentId = std::nullopt;
        i64 lastCategoryId = 0;

        for (const auto& name : chain) {
            auto existingId = m_modelRepo.findCategoryByNameAndParent(name, parentId);
            if (existingId) {
                lastCategoryId = *existingId;
            } else {
                auto newId = createCategory(name, parentId);
                if (!newId) {
                    log::warningf("LibraryMgr", "Failed to create category: %s", name.c_str());
                    return false;
                }
                lastCategoryId = *newId;
            }
            parentId = lastCategoryId;
        }
        return assignCategory(modelId, lastCategoryId);
    };

    // Only the first level can fan out (root categories).
    // Deeper levels stay as-is to avoid combinatorial explosion.
    bool ok = true;
    for (const auto& rootName : levels[0]) {
        std::vector<std::string> chain = {rootName};
        for (size_t i = 1; i < levels.size(); ++i) {
            chain.push_back(levels[i][0]); // use first name at deeper levels
        }
        ok = resolveChain(chain) && ok;
    }
    return ok;
}

} // namespace dw

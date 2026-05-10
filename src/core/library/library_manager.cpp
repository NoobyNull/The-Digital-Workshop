#include "library_manager.h"

#include "../../render/thumbnail_generator.h"
#include "../graph/graph_manager.h"
#include "../loaders/loader_factory.h"
#include "../mesh/hash.h"
#include "../paths/app_paths.h"
#include "../paths/path_resolver.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <sstream>
#include <unordered_set>

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

bool LibraryManager::clearAiClassification(i64 modelId) {
    return m_modelRepo.clearAiClassification(modelId);
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

static std::string lowerCategoryName(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static std::string trimCategoryName(const std::string& value) {
    size_t start = value.find_first_not_of(" \t");
    size_t end = value.find_last_not_of(" \t");
    if (start == std::string::npos || end == std::string::npos)
        return {};
    return value.substr(start, end - start + 1);
}

static std::string titleCategoryName(const std::string& value) {
    std::istringstream in(value);
    std::string word;
    std::string out;
    while (in >> word) {
        if (!out.empty())
            out += ' ';
        word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
        for (size_t i = 1; i < word.size(); ++i)
            word[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(word[i])));
        out += word;
    }
    return out;
}

static std::optional<std::string> workflowCategoryReplacement(const std::string& name) {
    static const std::unordered_set<std::string> kDrop = {
        "cnc",
        "cnc model",
        "cnc models",
        "3d model",
        "3d models",
        "3d print",
        "3d print model",
        "3d printing",
        "3d printable models",
    };
    const auto lower = lowerCategoryName(name);
    if (kDrop.count(lower) > 0)
        return "";
    if (lower == "cnc art")
        return "Art";
    if (lower == "cnc decorative")
        return "Decor";
    if (lower == "cnc parts")
        return "Functional";
    if (lower.find("cnc") != std::string::npos || lower.find("3d print") != std::string::npos ||
        lower.find("3d printed") != std::string::npos ||
        lower.find("3d printable") != std::string::npos || lower == "3d") {
        std::string cleaned = lower;
        for (const auto& token : {"cnc", "3d", "printed", "printable", "print", "prints",
                                  "model", "models", "object", "objects", "file", "files",
                                  "carving", "carvings", "carved", "design", "designs"}) {
            size_t pos = 0;
            while ((pos = cleaned.find(token, pos)) != std::string::npos)
                cleaned.replace(pos, std::strlen(token), " ");
        }
        cleaned = trimCategoryName(cleaned);
        if (cleaned.empty() || cleaned == "art")
            return "";
        if (cleaned == "decorative")
            return "Decor";
        return titleCategoryName(cleaned);
    }
    return std::nullopt;
}

static std::optional<std::vector<std::string>> nonPrimaryRootTargetPath(const std::string& name) {
    const auto lower = lowerCategoryName(name);
    if (lower == "decorative" || lower == "decor")
        return std::vector<std::string>{"Decor"};
    if (lower == "decorative elements")
        return std::vector<std::string>{"Decor", "Decorative Elements"};
    if (lower == "decorative hardware")
        return std::vector<std::string>{"Decor", "Hardware"};
    if (lower == "decorative panel" || lower == "decorative panels")
        return std::vector<std::string>{"Decor", "Panels"};
    if (lower == "decorative element")
        return std::vector<std::string>{"Decor", "Decorative Elements"};
    if (lower == "decorative object")
        return std::vector<std::string>{"Decor", "Decorative Objects"};
    if (lower == "wall art" || lower == "wall decor")
        return std::vector<std::string>{"Decor", "Wall Art"};
    if (lower == "relief")
        return std::vector<std::string>{"Art", "Relief"};
    if (lower == "relief panel")
        return std::vector<std::string>{"Art", "Relief", "Panels"};
    if (lower == "relief sculpture")
        return std::vector<std::string>{"Art", "Relief", "Sculpture"};
    if (lower == "sculpture")
        return std::vector<std::string>{"Art", "Sculpture"};
    if (lower == "statue")
        return std::vector<std::string>{"Art", "Statue"};
    if (lower == "ornamental")
        return std::vector<std::string>{"Decor", "Ornamental"};
    if (lower == "architectural")
        return std::vector<std::string>{"Architecture"};
    if (lower == "animal")
        return std::vector<std::string>{"Animals"};
    if (lower == "animal model")
        return std::vector<std::string>{"Animals"};
    if (lower == "abstract")
        return std::vector<std::string>{"Art", "Abstract"};
    if (lower == "badge" || lower == "badges")
        return std::vector<std::string>{"Symbols", "Badges"};
    if (lower == "character" || lower == "characters")
        return std::vector<std::string>{"People", "Characters"};
    if (lower == "character figurine")
        return std::vector<std::string>{"People", "Characters", "Figurines"};
    if (lower == "character masks")
        return std::vector<std::string>{"People", "Characters", "Masks"};
    if (lower == "character sculptures" || lower == "character statue")
        return std::vector<std::string>{"People", "Characters", "Sculpture"};
    if (lower == "coat of arms")
        return std::vector<std::string>{"Symbols", "Coat of Arms"};
    if (lower == "creature" || lower == "creatures" || lower == "fantasy creature")
        return std::vector<std::string>{"Fantasy", "Creatures"};
    if (lower == "fantasy creatures")
        return std::vector<std::string>{"Fantasy", "Creatures"};
    if (lower == "emblem")
        return std::vector<std::string>{"Symbols", "Emblems"};
    if (lower == "figures")
        return std::vector<std::string>{"People", "Figures"};
    if (lower == "figurine" || lower == "figurines")
        return std::vector<std::string>{"People", "Figurines"};
    if (lower == "hardware")
        return std::vector<std::string>{"Functional", "Hardware"};
    if (lower == "home decor")
        return std::vector<std::string>{"Decor"};
    if (lower == "humanoid figures" || lower == "human figures")
        return std::vector<std::string>{"People", "Figures"};
    if (lower == "leaf")
        return std::vector<std::string>{"Nature", "Botanical", "Leaf"};
    if (lower == "miniature")
        return std::vector<std::string>{"Art", "Miniatures"};
    if (lower == "miniatures")
        return std::vector<std::string>{"Art", "Miniatures"};
    if (lower == "mirror")
        return std::vector<std::string>{"Decor", "Mirrors"};
    if (lower == "monster")
        return std::vector<std::string>{"Fantasy", "Monsters"};
    if (lower == "mythological creatures")
        return std::vector<std::string>{"Fantasy", "Mythological Creatures"};
    if (lower == "parts" || lower == "utility")
        return std::vector<std::string>{"Functional"};
    if (lower == "furniture parts")
        return std::vector<std::string>{"Furniture", "Parts"};
    if (lower == "mechanical parts")
        return std::vector<std::string>{"Mechanical", "Parts"};
    if (lower == "model" || lower == "models" || lower == "model library")
        return std::vector<std::string>{};
    if (lower == "portrait")
        return std::vector<std::string>{"People", "Portraits"};
    if (lower == "religious art")
        return std::vector<std::string>{"Religion", "Art"};
    if (lower == "religious")
        return std::vector<std::string>{"Religion"};
    if (lower == "religious symbol" || lower == "religious symbols")
        return std::vector<std::string>{"Religion", "Symbols"};
    if (lower == "historical symbols")
        return std::vector<std::string>{"Symbols", "Historical"};
    if (lower == "law enforcement symbol")
        return std::vector<std::string>{"Symbols", "Law Enforcement"};
    if (lower == "logos")
        return std::vector<std::string>{"Symbols", "Logos"};
    if (lower == "military insignia")
        return std::vector<std::string>{"Military", "Insignia"};
    if (lower == "superhero")
        return std::vector<std::string>{"Pop Culture", "Superheroes"};
    if (lower == "vehicle")
        return std::vector<std::string>{"Vehicles"};
    if (lower == "video game")
        return std::vector<std::string>{"Pop Culture", "Video Games"};
    if (lower == "wall mounts")
        return std::vector<std::string>{"Functional", "Wall Mounts"};
    if (lower == "woodworking")
        return std::vector<std::string>{"Functional", "Woodworking"};
    if (lower == "fantasy")
        return std::vector<std::string>{"Fantasy"};
    if (lower == "police badges")
        return std::vector<std::string>{"Symbols", "Badges", "Police"};
    if (lower == "sculptures")
        return std::vector<std::string>{"Art", "Sculpture"};
    if (lower == "statues")
        return std::vector<std::string>{"Art", "Statue"};
    if (lower == "tools")
        return std::vector<std::string>{"Functional", "Tools"};
    if (lower == "transportation")
        return std::vector<std::string>{"Vehicles"};
    if (lower == "wheels")
        return std::vector<std::string>{"Vehicles", "Wheels"};
    if (lower == "kitchenware")
        return std::vector<std::string>{"Kitchen"};
    if (lower == "column")
        return std::vector<std::string>{"Architecture", "Columns"};
    if (lower == "head sculpture")
        return std::vector<std::string>{"Art", "Sculpture", "Heads"};
    if (lower == "butterfly")
        return std::vector<std::string>{"Animals", "Insects", "Butterfly"};
    if (lower == "sons of anarchy prop")
        return std::vector<std::string>{"Pop Culture", "Sons of Anarchy"};
    if (lower == "lethal company merchandise")
        return std::vector<std::string>{"Pop Culture", "Lethal Company"};
    if (lower == "back view")
        return std::vector<std::string>{};
    return std::nullopt;
}

static void appendCategoryPathPart(std::vector<std::string>& output, const std::string& value) {
    std::string trimmed = trimCategoryName(value);
    if (trimmed.empty())
        return;
    if (std::find(output.begin(), output.end(), trimmed) != output.end())
        return;
    output.push_back(trimmed);
}

static std::vector<std::string> canonicalizeAutoCategoryChain(
    const std::vector<std::string>& input) {
    constexpr size_t kMaxDepth = 4;
    std::vector<std::string> output;
    if (input.empty())
        return output;

    auto appendPath = [&](const std::vector<std::string>& path) {
        for (const auto& part : path) {
            appendCategoryPathPart(output, part);
            if (output.size() >= kMaxDepth)
                return;
        }
    };

    for (size_t i = 0; i < input.size() && output.size() < kMaxDepth; ++i) {
        auto workflowReplacement = workflowCategoryReplacement(input[i]);
        if (workflowReplacement) {
            if (!workflowReplacement->empty())
                appendCategoryPathPart(output, *workflowReplacement);
            continue;
        }
        auto targetPath = nonPrimaryRootTargetPath(input[i]);
        if (targetPath) {
            appendPath(*targetPath);
            continue;
        }
        appendCategoryPathPart(output, input[i]);
    }

    return output;
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

    // (b) Remove library/file/workflow buckets from category hierarchy.
    // These labels describe the whole library, not what a user would search for.
    {
        auto findOrCreateUnderParent = [&](const std::string& name,
                                           std::optional<i64> parentId) -> std::optional<i64> {
            auto existing = m_modelRepo.findCategoryByNameAndParent(name, parentId);
            if (existing)
                return existing;
            return createCategory(name, parentId);
        };

        auto directModelIds = [&](i64 categoryId) {
            std::vector<i64> ids;
            auto stmt =
                m_db.prepare("SELECT model_id FROM model_categories WHERE category_id = ?");
            if (!stmt.isValid() || !stmt.bindInt(1, categoryId))
                return ids;
            while (stmt.step())
                ids.push_back(stmt.getInt(0));
            return ids;
        };

        auto reparentCategory = [&](i64 categoryId, std::optional<i64> parentId) {
            auto stmt = parentId
                            ? m_db.prepare("UPDATE categories SET parent_id = ? WHERE id = ?")
                            : m_db.prepare("UPDATE categories SET parent_id = NULL WHERE id = ?");
            if (!stmt.isValid())
                return false;
            if (parentId) {
                if (!stmt.bindInt(1, *parentId) || !stmt.bindInt(2, categoryId))
                    return false;
            } else if (!stmt.bindInt(1, categoryId)) {
                return false;
            }
            return stmt.execute();
        };

        bool changed = true;
        while (changed) {
            changed = false;
            auto allCats = getAllCategories();
            for (const auto& cat : allCats) {
                auto replacement = workflowCategoryReplacement(cat.name);
                if (!replacement)
                    continue;

                std::optional<i64> targetParent = cat.parentId;
                std::optional<i64> directAssignmentTarget;
                if (!replacement->empty()) {
                    auto replacementId = findOrCreateUnderParent(*replacement, cat.parentId);
                    if (!replacementId)
                        continue;
                    targetParent = replacementId;
                    directAssignmentTarget = replacementId;
                }

                if (directAssignmentTarget) {
                    for (auto modelId : directModelIds(cat.id))
                        assignCategory(modelId, *directAssignmentTarget);
                }

                for (const auto& child : m_modelRepo.getChildCategories(cat.id)) {
                    auto existingSibling =
                        m_modelRepo.findCategoryByNameAndParent(child.name, targetParent);
                    if (existingSibling && *existingSibling != child.id) {
                        for (const auto& model : m_modelRepo.findByCategory(child.id))
                            assignCategory(model.id, *existingSibling);
                        deleteCategory(child.id);
                    } else {
                        reparentCategory(child.id, targetParent);
                    }
                }

                deleteCategory(cat.id);
                report.categoriesRemoved++;
                changed = true;
                break;
            }
        }
    }

    // (c) Move generic object/style roots under stable browse roots.
    {
        auto findOrCreateUnderParent = [&](const std::string& name,
                                           std::optional<i64> parentId) -> std::optional<i64> {
            auto existing = m_modelRepo.findCategoryByNameAndParent(name, parentId);
            if (existing)
                return existing;
            return createCategory(name, parentId);
        };

        auto resolvePath = [&](const std::vector<std::string>& path) -> std::optional<i64> {
            std::optional<i64> parentId = std::nullopt;
            std::optional<i64> current;
            for (const auto& name : path) {
                current = findOrCreateUnderParent(name, parentId);
                if (!current)
                    return std::nullopt;
                parentId = current;
            }
            return current;
        };

        auto directModelIds = [&](i64 categoryId) {
            std::vector<i64> ids;
            auto stmt =
                m_db.prepare("SELECT model_id FROM model_categories WHERE category_id = ?");
            if (!stmt.isValid() || !stmt.bindInt(1, categoryId))
                return ids;
            while (stmt.step())
                ids.push_back(stmt.getInt(0));
            return ids;
        };

        auto reparentCategory = [&](i64 categoryId, i64 parentId) {
            auto stmt = m_db.prepare("UPDATE categories SET parent_id = ? WHERE id = ?");
            if (!stmt.isValid())
                return false;
            if (!stmt.bindInt(1, parentId) || !stmt.bindInt(2, categoryId))
                return false;
            return stmt.execute();
        };

        bool changed = true;
        while (changed) {
            changed = false;
            auto allCats = getAllCategories();
            for (const auto& cat : allCats) {
                if (cat.parentId.has_value())
                    continue;
                auto targetPath = nonPrimaryRootTargetPath(cat.name);
                if (!targetPath)
                    continue;
                auto targetId = resolvePath(*targetPath);
                if (!targetId || *targetId == cat.id)
                    continue;

                for (auto modelId : directModelIds(cat.id))
                    assignCategory(modelId, *targetId);

                for (const auto& child : m_modelRepo.getChildCategories(cat.id)) {
                    auto existingSibling =
                        m_modelRepo.findCategoryByNameAndParent(child.name, *targetId);
                    if (existingSibling && *existingSibling != child.id) {
                        for (const auto& model : m_modelRepo.findByCategory(child.id))
                            assignCategory(model.id, *existingSibling);
                        deleteCategory(child.id);
                    } else {
                        reparentCategory(child.id, *targetId);
                    }
                }

                deleteCategory(cat.id);
                report.categoriesRemoved++;
                changed = true;
                break;
            }
        }
    }

    // (d) Delete empty categories (leaf-first via recursive approach)
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
    auto canonicalChain = canonicalizeAutoCategoryChain(categoryChain);
    if (canonicalChain.empty()) {
        return true;
    }

    // Expand any compound names (e.g. "Art & Decor" -> ["Art", "Decor"])
    // at each level, creating parallel chains.
    // Example: ["Art & Design", "Figurine"] produces:
    //   Art > Figurine  (assigned)
    //   Design > Figurine  (assigned)

    // Build list of expanded levels
    std::vector<std::vector<std::string>> levels;
    for (const auto& catName : canonicalChain) {
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

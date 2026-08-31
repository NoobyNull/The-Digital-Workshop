#include "project_asset_membership.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <nlohmann/json.hpp>
#include "../database/database.h"
#include "../database/gcode_repository.h"
#include "../database/model_repository.h"
#include "../utils/file_utils.h"
#include "../utils/log.h"
#include "../utils/string_utils.h"
#include "project.h"
#include "project_directory.h"
namespace dw {
namespace {
struct PreparedAsset {
    ProjectAssetRef ref;
    bool alreadyMember = false;
    bool hasOpenItem = false;
    Path sourcePath;
    std::optional<ModelRecord> model;
    std::optional<GCodeRecord> gcode;
};
Path normalizedPath(const Path& path) {
    std::error_code error;
    const Path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}
bool samePath(const Path& lhs, const Path& rhs) {
    return normalizedPath(lhs) == normalizedPath(rhs);
}
bool validOpenItem(const ProjectOpenItem& item, i64 projectId, ProjectAssetRef asset) {
    if (item.projectId != projectId || !item.sourceId.has_value() ||
        *item.sourceId != asset.sourceId) {
        return false;
    }
    if (asset.kind == ProjectAssetKind::Model) {
        return item.itemType == ProjectOpenItemType::Model && item.sourceTable == "models";
    }
    return item.itemType == ProjectOpenItemType::Gcode && item.sourceTable == "gcode_files";
}
std::string sourceTable(ProjectAssetKind kind) {
    return kind == ProjectAssetKind::Model ? "models" : "gcode_files";
}
template <typename Value>
std::string jsonArray(const std::vector<Value>& values) {
    std::ostringstream out;
    out << '[';
    for (usize index = 0; index < values.size(); ++index)
        out << (index > 0 ? "," : "") << values[index];
    out << ']';
    return out.str();
}
ProjectOpenItem makeOpenItem(i64 projectId, const PreparedAsset& asset) {
    ProjectOpenItem item;
    item.projectId = projectId;
    item.sourceId = asset.ref.sourceId;
    item.status = ProjectOpenItemStatus::Ready;
    if (asset.ref.kind == ProjectAssetKind::Model) {
        const auto& model = *asset.model;
        item.itemType = ProjectOpenItemType::Model;
        item.sourceTable = "models";
        item.displayName = model.name;
        item.intentJson = R"({"role":"project_model"})";
        item.snapshotJson = nlohmann::json{
            {"hash", model.hash},
            {"file_path", model.filePath.string()},
            {"file_format", model.fileFormat},
            {"bounds",
             {{"min", {model.boundsMin.x, model.boundsMin.y, model.boundsMin.z}},
              {"max", {model.boundsMax.x, model.boundsMax.y, model.boundsMax.z}}}},
        }.dump();
        return item;
    }
    const auto& gcode = *asset.gcode;
    item.itemType = ProjectOpenItemType::Gcode;
    item.sourceTable = "gcode_files";
    item.displayName = gcode.name;
    item.intentJson = R"({"role":"project_gcode"})";
    std::ostringstream snapshot;
    snapshot << "{\"hash\":\"" << str::escapeJsonString(gcode.hash)
             << "\",\"file_path\":\"" << str::escapeJsonString(gcode.filePath.string())
             << "\",\"file_size\":" << gcode.fileSize
             << ",\"estimated_time\":" << gcode.estimatedTime
             << ",\"feed_rates\":" << jsonArray(gcode.feedRates)
             << ",\"tool_numbers\":" << jsonArray(gcode.toolNumbers) << '}';
    item.snapshotJson = snapshot.str();
    return item;
}
Path uniqueSibling(const Path& root, std::string_view role, i64 projectId) {
    static std::atomic<unsigned long long> sequence{0};
    const auto value = sequence.fetch_add(1);
    return root.parent_path() /
           (".dw-membership-" + std::string(role) + "-" + std::to_string(projectId) + "-" +
            std::to_string(value));
}
std::optional<Path> cloneProjectDirectory(const Path& root, i64 projectId) {
    if (root.empty() || root.parent_path().empty() || std::filesystem::is_symlink(root))
        return std::nullopt;
    Path staging;
    std::error_code error;
    bool owned = false;
    for (int attempt = 0; attempt < 100 && !owned; ++attempt) {
        staging = uniqueSibling(root, "staging", projectId);
        error.clear();
        owned = std::filesystem::create_directory(staging, error);
    }
    if (!owned)
        return std::nullopt;
    std::filesystem::copy(root,
                          staging,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::copy_symlinks,
                          error);
    if (!error)
        return staging;
    std::error_code cleanupError;
    std::filesystem::remove_all(staging, cleanupError);
    log::errorf("ProjectAssets", "Could not stage project directory: %s", error.message().c_str());
    return std::nullopt;
}

void removeOwnedDirectory(const Path& path) {
    if (path.empty())
        return;
    std::error_code error;
    std::filesystem::remove_all(path, error);
    if (error) {
        log::warningf("ProjectAssets",
                      "Could not remove owned staging directory: %s",
                      error.message().c_str());
    }
}

struct PublishResult {
    bool published = false;
    bool restoreFailed = false;
    Path backup;
};

PublishResult publishStagedDirectory(const Path& root, const Path& staging, i64 projectId) {
    PublishResult result;
    result.backup = uniqueSibling(root, "backup", projectId);

    std::error_code error;
    std::filesystem::rename(root, result.backup, error);
    if (error) {
        log::errorf("ProjectAssets", "Could not stage live directory: %s", error.message().c_str());
        return result;
    }

    error.clear();
    std::filesystem::rename(staging, root, error);
    if (!error) {
        result.published = true;
        return result;
    }

    log::errorf("ProjectAssets", "Could not publish staged directory: %s", error.message().c_str());
    std::error_code restoreError;
    std::filesystem::rename(result.backup, root, restoreError);
    result.restoreFailed = static_cast<bool>(restoreError);
    return result;
}

bool restorePublishedDirectory(const Path& root, const Path& backup, i64 projectId) {
    const Path failed = uniqueSibling(root, "failed", projectId);
    std::error_code moveError;
    std::filesystem::rename(root, failed, moveError);
    if (moveError)
        return false;

    std::error_code restoreError;
    std::filesystem::rename(backup, root, restoreError);
    if (restoreError) {
        std::error_code putBackError;
        std::filesystem::rename(failed, root, putBackError);
        return false;
    }

    removeOwnedDirectory(failed);
    return true;
}
void noteFailure(ProjectAssetMembershipResult& result, ProjectAssetMembershipFailure failure) {
    if (result.failure == ProjectAssetMembershipFailure::None)
        result.failure = failure;
}

} // namespace

ProjectAssetMembershipResult
ProjectAssetMembershipService::ensure(const ProjectAssetMembershipRequest& request) {
    ProjectAssetMembershipResult result;
    result.items.reserve(request.assets.size());
    if (request.assets.empty()) {
        result.failure = ProjectAssetMembershipFailure::EmptyRequest;
        return result;
    }

    ProjectAssetMembershipRequest normalized;
    normalized.expectedProjectId = request.expectedProjectId;
    std::vector<usize> canonicalOutcomeIndexes;

    for (const auto asset : request.assets) {
        const auto existing =
            std::find(normalized.assets.begin(), normalized.assets.end(), asset);
        if (existing != normalized.assets.end()) {
            result.items.push_back(
                {asset, ProjectAssetMembershipItemStatus::DuplicateRequest});
            continue;
        }

        normalized.assets.push_back(asset);
        canonicalOutcomeIndexes.push_back(result.items.size());
        const bool validKind = asset.kind == ProjectAssetKind::Model ||
                               asset.kind == ProjectAssetKind::GCode;
        const auto status = !validKind
                                ? ProjectAssetMembershipItemStatus::InvalidAssetKind
                            : asset.sourceId > 0
                                ? ProjectAssetMembershipItemStatus::NotCommitted
                                : ProjectAssetMembershipItemStatus::InvalidSourceId;
        result.items.push_back({asset, status});
        if (!validKind)
            noteFailure(result, ProjectAssetMembershipFailure::InvalidAssetKind);
        else if (asset.sourceId <= 0)
            noteFailure(result, ProjectAssetMembershipFailure::InvalidSourceId);
    }

    if (result.failure != ProjectAssetMembershipFailure::None)
        return result;

    auto persisted = m_projects.persistAssetMembership(normalized);
    if (persisted.items.size() != canonicalOutcomeIndexes.size()) {
        result.failure = ProjectAssetMembershipFailure::VerificationFailed;
        return result;
    }

    for (usize index = 0; index < persisted.items.size(); ++index) {
        result.items[canonicalOutcomeIndexes[index]].status = persisted.items[index].status;
    }
    result.status = persisted.status;
    result.failure = persisted.failure;
    return result;
}

ProjectAssetMembershipResult
ProjectManager::persistAssetMembership(const ProjectAssetMembershipRequest& request) {
    ProjectAssetMembershipResult result;
    result.items.reserve(request.assets.size());
    for (const auto asset : request.assets)
        result.items.push_back({asset, ProjectAssetMembershipItemStatus::NotCommitted});

    const auto pinnedProject = m_currentProject;
    const auto pinnedDirectory = m_currentDir;
    if (!pinnedProject) {
        result.failure = ProjectAssetMembershipFailure::NoActiveProject;
        return result;
    }
    if (request.expectedProjectId <= 0 || pinnedProject->id() != request.expectedProjectId) {
        result.failure = ProjectAssetMembershipFailure::ProjectMismatch;
        return result;
    }
    if (!pinnedDirectory ||
        validateProjectStorage(request.expectedProjectId) != ProjectStorageValidationStatus::Ready) {
        result.failure = ProjectAssetMembershipFailure::StorageUnavailable;
        return result;
    }

    const auto projectRecord = m_projectRepo.findById(request.expectedProjectId);
    if (!projectRecord || projectRecord->filePath.empty() ||
        !samePath(projectRecord->filePath, pinnedProject->filePath()) ||
        !samePath(projectRecord->filePath, pinnedDirectory->root())) {
        result.failure = ProjectAssetMembershipFailure::ProjectMismatch;
        return result;
    }

    // Interactive project work has one design. Keep the plural repository and
    // manifest representation for imported/legacy projects, but never use the
    // ordinary membership gateway to create another model association. An
    // idempotent request for any already-associated legacy model remains safe.
    const auto existingModelIds = m_projectRepo.getModelIds(request.expectedProjectId);

    ModelRepository models(m_db);
    GCodeRepository gcode(m_db);
    std::vector<PreparedAsset> prepared;
    prepared.reserve(request.assets.size());
    bool allExisting = true;

    for (usize index = 0; index < request.assets.size(); ++index) {
        const auto asset = request.assets[index];
        PreparedAsset value;
        value.ref = asset;

        if (asset.kind == ProjectAssetKind::Model) {
            value.model = models.findById(asset.sourceId);
            if (!value.model) {
                result.items[index].status = ProjectAssetMembershipItemStatus::SourceMissing;
                noteFailure(result, ProjectAssetMembershipFailure::SourceMissing);
                prepared.push_back(std::move(value));
                continue;
            }
            value.sourcePath = resolveModelPath(*value.model);
            value.alreadyMember = m_projectRepo.hasModel(request.expectedProjectId, asset.sourceId);
        } else {
            value.gcode = gcode.findById(asset.sourceId);
            if (!value.gcode) {
                result.items[index].status = ProjectAssetMembershipItemStatus::SourceMissing;
                noteFailure(result, ProjectAssetMembershipFailure::SourceMissing);
                prepared.push_back(std::move(value));
                continue;
            }
            value.sourcePath = resolveGCodePath(*value.gcode);
            value.alreadyMember = gcode.isInProject(request.expectedProjectId, asset.sourceId);
        }

        if (!file::isFile(value.sourcePath)) {
            result.items[index].status = ProjectAssetMembershipItemStatus::SourceFileMissing;
            noteFailure(result, ProjectAssetMembershipFailure::SourceFileMissing);
            prepared.push_back(std::move(value));
            continue;
        }

        const auto openItems = m_projectRepo.findOpenItemsBySource(
            request.expectedProjectId, sourceTable(asset.kind), asset.sourceId);
        value.hasOpenItem = openItems.size() == 1 &&
                            validOpenItem(openItems.front(), request.expectedProjectId, asset);
        if ((value.alreadyMember && !value.hasOpenItem) || openItems.size() > 1 ||
            (!openItems.empty() && !value.hasOpenItem)) {
            result.items[index].status = ProjectAssetMembershipItemStatus::OpenItemMissing;
            noteFailure(result, ProjectAssetMembershipFailure::OpenItemMissing);
            prepared.push_back(std::move(value));
            continue;
        }

        result.items[index].status = value.alreadyMember
                                         ? ProjectAssetMembershipItemStatus::AlreadyMember
                                         : ProjectAssetMembershipItemStatus::NotCommitted;
        allExisting &= value.alreadyMember;
        prepared.push_back(std::move(value));
    }

    if (result.failure != ProjectAssetMembershipFailure::None)
        return result;

    std::vector<usize> newModelIndexes;
    for (usize index = 0; index < prepared.size(); ++index) {
        if (prepared[index].ref.kind == ProjectAssetKind::Model &&
            !prepared[index].alreadyMember) {
            newModelIndexes.push_back(index);
        }
    }
    const bool wouldAddSecondModel =
        (!existingModelIds.empty() && !newModelIndexes.empty()) ||
        (existingModelIds.empty() && newModelIndexes.size() > 1);
    if (wouldAddSecondModel) {
        for (const auto index : newModelIndexes)
            result.items[index].status = ProjectAssetMembershipItemStatus::ModelLimitExceeded;
        result.failure = ProjectAssetMembershipFailure::ModelLimitExceeded;
        return result;
    }
    if (allExisting) {
        result.status = ProjectAssetMembershipStatus::Unchanged;
        return result;
    }

    // Projection covers every persisted member, so validate those source rows
    // and files before opening the transaction as well as each requested asset.
    for (const i64 modelId : m_projectRepo.getModelIds(request.expectedProjectId)) {
        const auto model = models.findById(modelId);
        if (!model || !file::isFile(resolveModelPath(*model))) {
            result.failure = ProjectAssetMembershipFailure::ProjectionFailed;
            return result;
        }
    }
    for (const auto& existingGCode : gcode.findByProject(request.expectedProjectId)) {
        if (!file::isFile(resolveGCodePath(existingGCode))) {
            result.failure = ProjectAssetMembershipFailure::ProjectionFailed;
            return result;
        }
    }

    Transaction transaction(m_db);
    if (!transaction.started()) {
        result.failure = ProjectAssetMembershipFailure::TransactionUnavailable;
        return result;
    }

    Path staging;
    PublishResult publication;
    auto fail = [&](ProjectAssetMembershipFailure failure, bool published = false) {
        bool storageRestored = true;
        if (published) {
            storageRestored = restorePublishedDirectory(
                projectRecord->filePath, publication.backup, request.expectedProjectId);
        }
        removeOwnedDirectory(staging);
        const bool databaseRestored = transaction.rollback();
        result.failure = storageRestored && databaseRestored
                             ? failure
                             : ProjectAssetMembershipFailure::RollbackFailed;
        return result;
    };

    for (const auto& asset : prepared) {
        if (asset.alreadyMember)
            continue;
        const bool associated = asset.ref.kind == ProjectAssetKind::Model
                                    ? m_projectRepo.addModel(request.expectedProjectId,
                                                             asset.ref.sourceId)
                                    : gcode.addToProject(request.expectedProjectId,
                                                         asset.ref.sourceId);
        if (!associated)
            return fail(ProjectAssetMembershipFailure::AssociationWriteFailed);

        if (!asset.hasOpenItem && !m_projectRepo.insertOpenItem(
                                      makeOpenItem(request.expectedProjectId, asset))) {
            return fail(ProjectAssetMembershipFailure::OpenItemWriteFailed);
        }
    }
    if (!m_projectRepo.updateModifiedTime(request.expectedProjectId))
        return fail(ProjectAssetMembershipFailure::AssociationWriteFailed);

    for (const auto& asset : prepared) {
        const bool associated = asset.ref.kind == ProjectAssetKind::Model
                                    ? m_projectRepo.hasModel(request.expectedProjectId,
                                                             asset.ref.sourceId)
                                    : gcode.isInProject(request.expectedProjectId,
                                                        asset.ref.sourceId);
        const auto openItems = m_projectRepo.findOpenItemsBySource(
            request.expectedProjectId, sourceTable(asset.ref.kind), asset.ref.sourceId);
        if (!associated || openItems.size() != 1 ||
            !validOpenItem(openItems.front(), request.expectedProjectId, asset.ref)) {
            return fail(ProjectAssetMembershipFailure::VerificationFailed);
        }
    }

    const auto currentRecord = m_projectRepo.findById(request.expectedProjectId);
    if (!currentRecord || !samePath(currentRecord->filePath, projectRecord->filePath))
        return fail(ProjectAssetMembershipFailure::VerificationFailed);

    const auto stagedRoot = cloneProjectDirectory(projectRecord->filePath, request.expectedProjectId);
    if (!stagedRoot)
        return fail(ProjectAssetMembershipFailure::ProjectionFailed);
    staging = *stagedRoot;

    ProjectDirectory stagedDirectory;
    if (!stagedDirectory.open(staging))
        return fail(ProjectAssetMembershipFailure::ProjectionFailed);
    stagedDirectory.setMetadata(currentRecord->name, currentRecord->description);
    stagedDirectory.setProjectId(request.expectedProjectId);
    stagedDirectory.clearModels();
    stagedDirectory.clearGCode();

    for (const i64 modelId : m_projectRepo.getModelIds(request.expectedProjectId)) {
        const auto model = models.findById(modelId);
        if (!model)
            return fail(ProjectAssetMembershipFailure::ProjectionFailed);
        const Path path = resolveModelPath(*model);
        const bool added = model->hash.empty() ? stagedDirectory.addModelFile(path)
                                                : stagedDirectory.addModelFile(path, model->hash);
        if (!added)
            return fail(ProjectAssetMembershipFailure::ProjectionFailed);
    }
    for (const auto& item : gcode.findByProject(request.expectedProjectId)) {
        if (!stagedDirectory.addGCodeFile(resolveGCodePath(item)))
            return fail(ProjectAssetMembershipFailure::ProjectionFailed);
    }
    if (!stagedDirectory.save())
        return fail(ProjectAssetMembershipFailure::ProjectionFailed);

    publication = publishStagedDirectory(
        projectRecord->filePath, staging, request.expectedProjectId);
    if (!publication.published) {
        const auto failure = publication.restoreFailed
                                 ? ProjectAssetMembershipFailure::RollbackFailed
                                 : ProjectAssetMembershipFailure::DirectoryPublishFailed;
        return fail(failure);
    }
    staging.clear();

    auto committedDirectory = std::make_shared<ProjectDirectory>();
    if (!committedDirectory->inspect(projectRecord->filePath) ||
        committedDirectory->projectId() != request.expectedProjectId) {
        return fail(ProjectAssetMembershipFailure::VerificationFailed, true);
    }

    const auto identityRecord = m_projectRepo.findById(request.expectedProjectId);
    if (m_currentProject != pinnedProject || m_currentDir != pinnedDirectory ||
        !identityRecord || !samePath(identityRecord->filePath, projectRecord->filePath) ||
        pinnedProject->id() != request.expectedProjectId ||
        !samePath(pinnedProject->filePath(), projectRecord->filePath) ||
        !samePath(pinnedDirectory->root(), projectRecord->filePath)) {
        return fail(ProjectAssetMembershipFailure::ActiveProjectChanged, true);
    }

    if (!transaction.commit())
        return fail(ProjectAssetMembershipFailure::TransactionCommitFailed, true);

    const bool wasModified = pinnedProject->isModified();
    for (const auto& asset : prepared) {
        if (!asset.alreadyMember && asset.ref.kind == ProjectAssetKind::Model)
            pinnedProject->addModel(asset.ref.sourceId);
    }
    if (!wasModified)
        pinnedProject->clearModified();
    m_currentDir = std::move(committedDirectory);

    removeOwnedDirectory(publication.backup);
    for (usize index = 0; index < prepared.size(); ++index) {
        if (!prepared[index].alreadyMember)
            result.items[index].status = ProjectAssetMembershipItemStatus::Added;
    }
    result.status = ProjectAssetMembershipStatus::Applied;
    result.failure = ProjectAssetMembershipFailure::None;
    return result;
}

} // namespace dw

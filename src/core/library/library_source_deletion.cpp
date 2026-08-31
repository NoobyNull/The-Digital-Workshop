#include "library_source_deletion.h"

#include <algorithm>
#include <string>

#include "../database/gcode_repository.h"
#include "../database/project_repository.h"
#include "library_manager.h"

namespace dw {
namespace {

bool isKnownKind(LibrarySourceKind kind) {
    return kind == LibrarySourceKind::Model || kind == LibrarySourceKind::GCode;
}

void appendProject(std::vector<LibrarySourceProjectRef>& projects,
                   const LibrarySourceProjectRef& project) {
    const auto existing = std::find_if(projects.begin(), projects.end(), [&](const auto& item) {
        return item.id == project.id;
    });
    if (existing == projects.end()) {
        projects.push_back(project);
    }
}

void sortProjects(std::vector<LibrarySourceProjectRef>& projects) {
    std::sort(projects.begin(), projects.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
    });
}

} // namespace

bool operator==(const LibrarySourceRef& lhs, const LibrarySourceRef& rhs) {
    return lhs.kind == rhs.kind && lhs.id == rhs.id;
}

LibrarySourceDeletionService::LibrarySourceDeletionService(LibraryManager& library,
                                                           ProjectRepository& projects,
                                                           GCodeRepository& gcodes)
    : m_library(library), m_projects(projects), m_gcodes(gcodes) {}

LibrarySourceDeletionResult LibrarySourceDeletionService::deleteSources(
    const std::vector<LibrarySourceRef>& sources, std::optional<LibrarySourceRef> activePreview) {
    LibrarySourceDeletionResult result;
    result.items.reserve(sources.size());

    if (sources.empty()) {
        return result;
    }

    std::vector<usize> canonicalItems;
    std::vector<usize> duplicateOf(sources.size(), sources.size());
    std::vector<bool> ready(sources.size(), false);
    bool blocked = false;

    for (usize index = 0; index < sources.size(); ++index) {
        LibrarySourceDeletionItemResult item;
        item.source = sources[index];

        const auto earlier = std::find(sources.begin(), sources.begin() + index, sources[index]);
        if (earlier != sources.begin() + index) {
            const usize earlierIndex = static_cast<usize>(std::distance(sources.begin(), earlier));
            duplicateOf[index] = earlierIndex;
            item.duplicate = true;
            result.items.push_back(std::move(item));
            continue;
        }

        canonicalItems.push_back(index);
        if (!isKnownKind(item.source.kind) || item.source.id <= 0) {
            item.status = LibrarySourceDeletionItemStatus::InvalidSource;
            blocked = true;
            result.items.push_back(std::move(item));
            continue;
        }

        const bool exists = item.source.kind == LibrarySourceKind::Model
                                ? m_library.getModel(item.source.id).has_value()
                                : m_library.getGCodeFile(item.source.id).has_value();
        if (!exists) {
            item.status = LibrarySourceDeletionItemStatus::MissingSource;
            blocked = true;
            result.items.push_back(std::move(item));
            continue;
        }

        const auto projectIds = item.source.kind == LibrarySourceKind::Model
                                    ? m_projects.getProjectsForModel(item.source.id)
                                    : m_gcodes.getProjectsForGCode(item.source.id);
        for (const i64 projectId : projectIds) {
            const auto record = m_projects.findById(projectId);
            const std::string name = record && !record->name.empty()
                                         ? record->name
                                         : "Project #" + std::to_string(projectId);
            const LibrarySourceProjectRef project{projectId, name};
            appendProject(item.affectedProjects, project);
            appendProject(result.affectedProjects, project);
        }
        sortProjects(item.affectedProjects);

        if (activePreview && item.source == *activePreview) {
            item.status = LibrarySourceDeletionItemStatus::ActivePreview;
            blocked = true;
        } else if (!item.affectedProjects.empty()) {
            item.status = LibrarySourceDeletionItemStatus::LinkedToProjects;
            blocked = true;
        } else {
            ready[index] = true;
        }
        result.items.push_back(std::move(item));
    }

    sortProjects(result.affectedProjects);
    if (blocked) {
        for (usize index = 0; index < result.items.size(); ++index) {
            if (duplicateOf[index] != sources.size()) {
                result.items[index].status = result.items[duplicateOf[index]].status;
                result.items[index].affectedProjects =
                    result.items[duplicateOf[index]].affectedProjects;
            } else if (ready[index]) {
                result.items[index].status = LibrarySourceDeletionItemStatus::BatchBlocked;
            }
        }
        result.status = LibrarySourceDeletionStatus::PreflightRejected;
        return result;
    }

    usize deletedCount = 0;
    for (const usize index : canonicalItems) {
        const auto& source = result.items[index].source;
        const bool removed = source.kind == LibrarySourceKind::Model
                                 ? m_library.removeModel(source.id)
                                 : m_library.deleteGCodeFile(source.id);
        const bool stillExists = source.kind == LibrarySourceKind::Model
                                     ? m_library.getModel(source.id).has_value()
                                     : m_library.getGCodeFile(source.id).has_value();
        if (removed && !stillExists) {
            result.items[index].status = LibrarySourceDeletionItemStatus::Deleted;
            ++deletedCount;
        } else {
            result.items[index].status = LibrarySourceDeletionItemStatus::DeleteFailed;
        }
    }

    for (usize index = 0; index < result.items.size(); ++index) {
        if (duplicateOf[index] != sources.size()) {
            result.items[index].status = result.items[duplicateOf[index]].status;
            result.items[index].affectedProjects =
                result.items[duplicateOf[index]].affectedProjects;
        }
    }

    if (deletedCount == canonicalItems.size()) {
        result.status = LibrarySourceDeletionStatus::Deleted;
    } else if (deletedCount == 0) {
        result.status = LibrarySourceDeletionStatus::DeletionFailed;
    } else {
        result.status = LibrarySourceDeletionStatus::PartiallyDeleted;
    }
    return result;
}

} // namespace dw

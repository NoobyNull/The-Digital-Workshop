// Generated G-code persistence for the Direct Carve workflow.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>
#include <optional>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/carve/carve_job.h"
#include "core/database/gcode_repository.h"
#include "core/gcode/gcode_document.h"
#include "core/library/library_manager.h"
#include "core/mesh/hash.h"
#include "core/paths/path_resolver.h"
#include "core/project/project.h"
#include "core/project/project_directory.h"
#include "core/utils/file_utils.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/panels/gcode_panel.h"
#include "ui/widgets/toast.h"

namespace dw {
namespace {

GCodeRecord makeGeneratedGCodeRecord(const Path& path,
                                     const std::string& name,
                                     const std::string& hash,
                                     const gcode::PreparedDocument& document) {
    GCodeRecord record;
    record.hash = hash;
    record.name = name;
    record.filePath = PathResolver::makeStorable(path, PathCategory::GCode);
    record.fileSize = file::fileSize(path);

    record.boundsMin = document.statistics.boundsMin;
    record.boundsMax = document.statistics.boundsMax;
    record.totalDistance = document.statistics.totalPathLength;
    record.estimatedTime = document.statistics.estimatedTime;
    record.feedRates = document.feedRates;
    record.toolNumbers = document.toolNumbers;

    return record;
}

} // anonymous namespace

bool DirectCarvePanel::hasCurrentToolpath() const {
    return m_carveJob && m_toolpathGenerated &&
           m_generatedAtVersion == m_settingsVersion &&
           m_carveJob->state() == carve::CarveJobState::Ready;
}

void DirectCarvePanel::saveGCodeToProject() {
    if (!hasCurrentToolpath()) {
        ToastManager::instance().show(
            ToastType::Warning,
            "Toolpath Not Ready",
            "Generate a current toolpath before saving G-code.");
        return;
    }
    if (!m_projectManager || !m_carveJob || !m_projectDirectoryRequest ||
        !m_preparationPin || !pinnedOperationOpenItem()) {
        showExportDialog();
        return;
    }

    const auto pin = *m_preparationPin;
    m_projectDirectoryRequest(
        pin,
        [this, pin](std::shared_ptr<ProjectDirectory> directory) {
            saveGCodeToProjectDirectory(pin, std::move(directory));
        });
}

void DirectCarvePanel::saveGCodeToProjectDirectory(
    carve_preparation::PrepareCarvePin pin,
    std::shared_ptr<ProjectDirectory> dir,
    std::function<void(bool)> completion) {
    const bool requestedForRun = static_cast<bool>(completion);
    auto finish = [&completion](bool success) {
        if (completion) completion(success);
    };
    m_savedRunToolpath.reset();
    if (!hasCurrentToolpath()) {
        ToastManager::instance().show(
            ToastType::Warning,
            "Toolpath Changed",
            "The project context changed before G-code could be saved.");
        finish(false);
        return;
    }
    if (!dir || dir->projectId() != pin.project().value || !m_preparationPin ||
        !(*m_preparationPin == pin) || !pinnedOperationOpenItem()) {
        ToastManager::instance().show(ToastType::Error,
                                      "Project Error",
                                      "The pinned project is no longer available. Export instead.");
        if (!requestedForRun) showExportDialog();
        finish(false);
        return;
    }

    std::string baseName = ProjectDirectory::sanitizeName(m_modelName);
    Path destPath = dir->gcodeDir() / (baseName + ".nc");
    const auto& finishingTool = m_toolPlan.finishingIntent();
    if (!finishingTool) {
        ToastManager::instance().show(
            ToastType::Error,
            "Finishing Tool Missing",
            "Choose a finishing tool before saving G-code.");
        finish(false);
        return;
    }
    const auto* prepared = ensureCurrentGCodeDocument();
    if (!prepared) {
        ToastManager::instance().show(
            ToastType::Error,
            "G-code Preparation Failed",
            "The generated path could not be prepared for preview or saving.");
        finish(false);
        return;
    }
    std::string toolName = resolveToolNameFormat(*finishingTool);

    if (!file::writeTextAtomic(destPath, prepared->exactText)) {
        ToastManager::instance().show(ToastType::Error,
                                      "Export Failed",
                                      "Could not write " + destPath.string());
        finish(false);
        return;
    }

    dir->addGCode(baseName + ".nc", toolName);
    dir->save();

    std::optional<i64> gcodeId;
    std::optional<GCodeRecord> savedRecord;
    if (m_gcodeRepo) {
        const auto fileHash = hash::computeFile(destPath);
        auto record = makeGeneratedGCodeRecord(
            destPath, baseName, fileHash, *prepared);

        auto existing = m_gcodeRepo->findByPath(record.filePath);
        if (!existing) {
            existing = m_gcodeRepo->findByHash(fileHash);
        }

        if (existing) {
            record.id = existing->id;
            if (m_gcodeRepo->update(record)) {
                gcodeId = record.id;
                savedRecord = record;
            }
        } else {
            gcodeId = m_gcodeRepo->insert(record);
            if (gcodeId) {
                record.id = *gcodeId;
                savedRecord = record;
            }
        }

        if (gcodeId) {
            m_gcodeRepo->addToProject(pin.project().value, *gcodeId);
        }
    }

    std::optional<i64> gcodeOpenItemId;
    if (gcodeId && savedRecord && m_projectManager && syncOperationOpenItem()) {
        nlohmann::json snapshot = {
            {"hash", savedRecord->hash},
            {"file_path", savedRecord->filePath.string()},
            {"file_size", savedRecord->fileSize},
            {"estimated_time", savedRecord->estimatedTime},
            {"total_distance", savedRecord->totalDistance},
            {"feed_rates", savedRecord->feedRates},
            {"tool_numbers", savedRecord->toolNumbers},
        };

        ProjectOpenItem item;
        item.projectId = pin.project().value;
        item.itemType = ProjectOpenItemType::Gcode;
        item.sourceTable = "gcode_files";
        item.sourceId = *gcodeId;
        item.sourceKey = "gcode_files:" + std::to_string(*gcodeId);
        item.parentItemId = pin.operationItem().item.value;
        item.status = ProjectOpenItemStatus::Ready;
        item.displayName = savedRecord->name;
        item.intentJson = R"({"role":"generated_direct_carve_program"})";
        item.snapshotJson = snapshot.dump();
        gcodeOpenItemId = m_projectManager->upsertOpenItem(std::move(item));
    }

    if (gcodeId && m_libraryManager) {
        const i64 modelId = pin.modelSource().item.value;
        if (modelId > 0) {
            auto groups = m_libraryManager->getOperationGroups(modelId);
            auto groupIt =
                std::find_if(groups.begin(), groups.end(), [](const OperationGroup& group) {
                    return group.name == "Direct Carve";
                });

            std::optional<i64> groupId;
            if (groupIt != groups.end()) {
                groupId = groupIt->id;
            } else {
                groupId = m_libraryManager->createOperationGroup(modelId,
                                                                 "Direct Carve",
                                                                 static_cast<int>(groups.size()));
            }

            if (groupId) {
                m_libraryManager->addGCodeToGroup(*groupId, *gcodeId);
            }
        }
    }

    if (m_gcodePanel) {
        m_gcodePanel->loadPreparedFile(destPath.string(), *prepared);
    }

    if (gcodeOpenItemId && savedRecord) {
        m_savedRunToolpath = SavedRunToolpath{
            workshop::ProjectItemRef{
                pin.project(), workshop::ProjectItemId(*gcodeOpenItemId)},
            static_cast<std::uint64_t>(m_generatedAtVersion + 1),
            savedRecord->hash,
            destPath};
    }

    if (!requestedForRun) {
        ToastManager::instance().show(
            ToastType::Success, "G-code Saved", destPath.string());
    }
    finish(m_savedRunToolpath.has_value() &&
           m_savedRunToolpath->gcodeItem.project == pin.project() &&
           m_savedRunToolpath->editRevision ==
               static_cast<std::uint64_t>(m_generatedAtVersion + 1));
}

void DirectCarvePanel::showExportDialog() {
    if (!m_fileDialog || !hasCurrentToolpath())
        return;
    if (!ensureCurrentGCodeDocument()) return;
    const auto prepared = m_preparedGCode;
    m_fileDialog->showSave(
        "Save G-code",
        {{"G-code Files", "*.nc;*.gcode;*.ngc"}},
        "carve.nc",
        [prepared](const std::string& path) {
            const bool ok = prepared &&
                            file::writeTextAtomic(Path(path), prepared->exactText);
            if (ok)
                ToastManager::instance().show(ToastType::Success, "G-code Saved", path);
            else
                ToastManager::instance().show(ToastType::Error,
                                              "Export Failed",
                                              "Could not write " + path);
        });
}

} // namespace dw

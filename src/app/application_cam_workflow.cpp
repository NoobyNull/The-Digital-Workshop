// Early CAM workflow (v0.8.0 slice): engine lifecycle off the UI thread,
// default-surfacing G-code generation, and persistence of the result as a
// normal project G-code item the Run boundary consumes unchanged.

#include "app/application.h"

#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/cam/cam_engine_client.h"
#include "core/cam/cam_engine_runtime.h"
#include "core/cam/cam_job_spec.h"
#include "core/config/config.h"
#include "core/database/gcode_repository.h"
#include "core/gcode/gcode_document.h"
#include "core/mesh/hash.h"
#include "core/paths/app_paths.h"
#include "core/paths/path_resolver.h"
#include "core/project/project.h"
#include "core/project/project_directory.h"
#include "core/threading/main_thread_queue.h"
#include "core/utils/file_utils.h"
#include "managers/ui_manager.h"
#include "ui/panels/gcode_panel.h"
#include "ui/widgets/toast.h"

namespace dw {
namespace {

void setGenerationMessage(Application::CamGenerationState& state, const std::string& message) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.message = message;
}

} // namespace

cam::CamEngineRuntime* Application::ensureCamEngineRuntime() {
    if (!m_camEngineRuntime) {
        cam::CamEngineConfig cfg;
        cfg.port = cam::bridgePortFromEnv(cfg.port);
        cfg.payloadDir = cam::locatePayloadDir(paths::getExeDir());
        m_camEngineRuntime = std::make_unique<cam::CamEngineRuntime>(cfg);
    }
    return m_camEngineRuntime.get();
}

void Application::startCamEngineAsync() {
    auto state = m_camGeneration;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->running)
            return;
        state->running = true;
        state->message = "Starting CAM engine...";
    }

    auto* runtime = ensureCamEngineRuntime();
    auto* queue = m_mainThreadQueue.get();
    std::thread([this, runtime, queue, state]() {
        auto status = runtime->ensureReady();
        std::vector<std::pair<std::string, std::string>> machines;
        if (status.ready) {
            for (const auto& machine :
                 cam::CamEngineClient(cam::baseUrl(runtime->config())).machines()) {
                machines.emplace_back(machine.id, machine.name);
            }
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->running = false;
            state->message.clear();
        }
        queue->enqueue(
            [this, status = std::move(status), machines = std::move(machines)]() mutable {
                m_camEngineStatus = std::move(status);
                if (!machines.empty())
                    m_camMachines = std::move(machines);
            });
    }).detach();
}

void Application::startCamGenerationAsync(const std::string& machineId,
                                          const std::string& orientation) {
    if (!m_camActiveSetup) {
        ToastManager::instance().show(
            ToastType::Warning,
            "CAM",
            "Choose a design in the Project Plan before generating G-code.");
        return;
    }

    auto state = m_camGeneration;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->running)
            return;
        state->running = true;
        state->message = "Starting CAM engine...";
    }

    cam::CamJobRequest request;
    request.modelName = m_camActiveSetup->modelName;
    request.meshPath = m_camActiveSetup->meshPath;
    request.machineId = machineId.empty() ? "fluidnc" : machineId;
    // "auto" lays the model flat for top-down carving; anything else is an
    // explicit engine axisSwap chosen in the panel.
    const Vec3& extents = m_camActiveSetup->extents;
    request.axisSwap = (orientation == "auto" || orientation.empty())
                           ? cam::layFlatAxisSwap(extents.x, extents.y, extents.z)
                           : orientation;
    const std::string spec = cam::buildDefaultSurfacingJobSpec(request);
    const CamActiveSetup setup = *m_camActiveSetup;

    auto* runtime = ensureCamEngineRuntime();
    auto* queue = m_mainThreadQueue.get();
    std::thread([this, runtime, queue, state, spec, setup]() {
        auto status = runtime->ensureReady();
        if (!status.ready) {
            setGenerationMessage(*state, status.reason);
            std::lock_guard<std::mutex> lock(state->mutex);
            state->running = false;
            return;
        }

        setGenerationMessage(*state, "Generating toolpaths...");
        const auto result = cam::CamEngineClient(cam::baseUrl(runtime->config())).submitJob(spec);
        if (!result.ok || result.gcode.empty()) {
            setGenerationMessage(*state,
                                 "Generation failed: " +
                                     (result.error.empty() ? "no G-code returned" : result.error));
            std::lock_guard<std::mutex> lock(state->mutex);
            state->running = false;
            return;
        }

        setGenerationMessage(*state, "Saving G-code to project...");
        queue->enqueue([this, state, setup, gcode = result.gcode]() mutable {
            persistGeneratedCamGCode(setup, std::move(gcode));
            std::lock_guard<std::mutex> lock(state->mutex);
            state->running = false;
        });
    }).detach();
}

void Application::persistGeneratedCamGCode(const CamActiveSetup& setup, std::string gcodeText) {
    auto fail = [this](const std::string& why) {
        setGenerationMessage(*m_camGeneration, "Save failed: " + why);
        ToastManager::instance().show(ToastType::Error, "CAM", why);
    };

    const auto project = m_projectManager ? m_projectManager->currentProject() : nullptr;
    const auto directory = m_projectManager ? m_projectManager->currentDirectory() : nullptr;
    if (!project || !directory || project->id() != setup.projectId) {
        fail("The project changed while G-code was generating.");
        return;
    }

    auto document = gcode::prepareDocument(std::move(gcodeText),
                                           Config::instance().getActiveMachineProfile());
    if (!document.hasCommands()) {
        fail("The engine returned no runnable G-code.");
        return;
    }

    const std::string baseName = ProjectDirectory::sanitizeName(setup.modelName) + "-cam";
    const Path destPath = directory->gcodeDir() / (baseName + ".nc");
    if (!file::writeTextAtomic(destPath, document.exactText)) {
        fail("Could not write " + destPath.string());
        return;
    }
    directory->addGCode(baseName + ".nc", "CAM engine default surfacing");
    directory->save();

    if (!m_gcodeRepo) {
        fail("The G-code library is unavailable.");
        return;
    }

    GCodeRecord record;
    record.hash = hash::computeFile(destPath);
    record.name = baseName;
    record.filePath = PathResolver::makeStorable(destPath, PathCategory::GCode);
    record.fileSize = file::fileSize(destPath);
    record.boundsMin = document.statistics.boundsMin;
    record.boundsMax = document.statistics.boundsMax;
    record.totalDistance = document.statistics.totalPathLength;
    record.estimatedTime = document.statistics.estimatedTime;
    record.feedRates = document.feedRates;
    record.toolNumbers = document.toolNumbers;

    std::optional<i64> gcodeId;
    auto existing = m_gcodeRepo->findByPath(record.filePath);
    if (!existing)
        existing = m_gcodeRepo->findByHash(record.hash);
    if (existing) {
        record.id = existing->id;
        if (m_gcodeRepo->update(record))
            gcodeId = record.id;
    } else {
        gcodeId = m_gcodeRepo->insert(record);
    }
    if (!gcodeId) {
        fail("The generated G-code could not be saved to the library.");
        return;
    }
    m_gcodeRepo->addToProject(setup.projectId, *gcodeId);

    ProjectOpenItem item;
    item.projectId = setup.projectId;
    item.itemType = ProjectOpenItemType::Gcode;
    item.sourceTable = "gcode_files";
    item.sourceId = *gcodeId;
    item.sourceKey = "gcode_files:" + std::to_string(*gcodeId);
    item.parentItemId = setup.operationItemId;
    item.status = ProjectOpenItemStatus::Generated;
    item.displayName = record.name;
    item.intentJson = R"({"role":"generated_cam_program"})";
    item.snapshotJson =
        nlohmann::json{
            {"hash", record.hash},
            {"file_path", record.filePath.string()},
            {"file_size", record.fileSize},
            {"estimated_time", record.estimatedTime},
            {"total_distance", record.totalDistance},
        }
            .dump();
    const auto itemId = m_projectManager ? m_projectManager->upsertOpenItem(std::move(item))
                                         : std::nullopt;
    if (!itemId) {
        fail("The G-code project item could not be recorded.");
        return;
    }

    m_camGeneratedItemId = *itemId;
    setGenerationMessage(*m_camGeneration, "G-code saved to project: " + record.name + ".nc");
    if (auto* panel = m_uiManager ? m_uiManager->gcodePanel() : nullptr) {
        panel->loadPreparedFile(destPath.string(), document);
    }
    ToastManager::instance().show(ToastType::Success,
                                  "CAM",
                                  "G-code ready: " + record.name + ".nc");
}

void Application::sendGeneratedCamGCodeToRun() {
    if (!m_camGeneratedItemId || !m_projectManager || !m_uiManager)
        return;
    const auto item = m_projectManager->findOpenItem(*m_camGeneratedItemId);
    if (!item) {
        ToastManager::instance().show(ToastType::Warning,
                                      "CAM",
                                      "The generated G-code item is no longer available.");
        return;
    }
    (void)activateProjectOpenItem(*item);
    // Explicit run intent crosses the Model->CNC workspace boundary; plain
    // item activation deliberately stops at geometry inspection.
    m_uiManager->openWindow("gcode_viewer");
}

} // namespace dw

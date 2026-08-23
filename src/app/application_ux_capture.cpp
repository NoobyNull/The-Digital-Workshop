#include "app/application.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <variant>

#include <glad/gl.h>
#include <nlohmann/json.hpp>
#include <SDL.h>
#include <stb_image_write.h>

#include "app/library_workflow_coordinator.h"
#include "app/project_session_integration.h"
#include "app/ux_capture_fixture.h"
#include "app/ux_capture_scenario.h"
#include "app/workspace.h"
#include "core/cnc/cnc_controller.h"
#include "core/config/config.h"
#include "core/config/layout_migration.h"
#include "core/library/library_manager.h"
#include "core/mesh/mesh.h"
#include "core/project/project.h"
#include "managers/ui_manager.h"
#include "modules/design_library/library_picker_flow.h"
#include "modules/project_session/project_session.h"
#include "modules/workshop/project_workshop_controller.h"
#include "ui/panels/direct_carve_panel.h"
#include "ui/panels/library_panel.h"
#include "ui/panels/project_panel.h"
#include "ui/panels/properties_panel.h"
#include "ui/panels/viewport_panel.h"
#include "version.h"

namespace dw {
namespace {

const char* routeName(workshop::WorkshopRoute route) {
    switch (route) {
    case workshop::WorkshopRoute::Home: return "Home";
    case workshop::WorkshopRoute::Project: return "Project";
    case workshop::WorkshopRoute::DesignLibrary: return "Design Library";
    case workshop::WorkshopRoute::RunCnc: return "Run CNC";
    }
    return "Unknown";
}

DirectCarveUxCaptureState directState(UxCaptureScenario scenario) {
    switch (scenario) {
    case UxCaptureScenario::PrepareDesignAndSize:
        return DirectCarveUxCaptureState::DesignAndSize;
    case UxCaptureScenario::PrepareMaterialAndBlank:
        return DirectCarveUxCaptureState::MaterialAndBlank;
    case UxCaptureScenario::PrepareChooseTool:
        return DirectCarveUxCaptureState::ChooseTool;
    case UxCaptureScenario::PrepareCarvePreview:
        return DirectCarveUxCaptureState::CarvePreview;
    case UxCaptureScenario::ReviewMissingRequirement:
        return DirectCarveUxCaptureState::ReviewMissing;
    case UxCaptureScenario::ReviewReady:
        return DirectCarveUxCaptureState::ReviewReady;
    case UxCaptureScenario::RunStreaming:
        return DirectCarveUxCaptureState::Streaming;
    case UxCaptureScenario::RunPausedAbortFocused:
        return DirectCarveUxCaptureState::Paused;
    default:
        return DirectCarveUxCaptureState::DesignAndSize;
    }
}

bool isDirectScenario(UxCaptureScenario scenario) {
    return static_cast<int>(scenario) >=
           static_cast<int>(UxCaptureScenario::PrepareDesignAndSize);
}

bool sameLibraryItem(workshop::LibraryItemRef lhs,
                     workshop::LibraryItemRef rhs) {
    return lhs.kind == rhs.kind && lhs.item == rhs.item;
}

} // namespace

bool Application::writeUxCaptureBackBuffer(const Path& outputPath,
                                            std::string& error) {
    if (outputPath.empty()) {
        error = "capture output path is required";
        return false;
    }
    int width = 0;
    int height = 0;
    SDL_GL_GetDrawableSize(m_window, &width, &height);
    if (width <= 0 || height <= 0) {
        error = "capture drawable has invalid dimensions";
        return false;
    }

    const auto rowBytes = static_cast<std::size_t>(width) * 4U;
    std::vector<unsigned char> pixels(
        rowBytes * static_cast<std::size_t>(height));
    glFinish();
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (glGetError() != GL_NO_ERROR) {
        error = "OpenGL front-buffer read failed";
        return false;
    }

    std::vector<unsigned char> topDown(pixels.size());
    for (int row = 0; row < height; ++row) {
        const auto source = static_cast<std::size_t>(height - 1 - row) * rowBytes;
        const auto target = static_cast<std::size_t>(row) * rowBytes;
        std::copy_n(pixels.data() + source, rowBytes, topDown.data() + target);
    }
    if (stbi_write_png(outputPath.string().c_str(), width, height, 4,
                       topDown.data(), static_cast<int>(rowBytes)) == 0) {
        error = "PNG writer could not save the capture";
        return false;
    }
    return true;
}

int Application::runUxCapture(const std::string& scenarioName,
                              int holdMilliseconds,
                              const Path& outputPath) {
    const auto descriptor = findUxCaptureScenario(scenarioName);
    auto fail = [&scenarioName](const std::string& reason) {
        std::fprintf(stderr, "DW_UX_CAPTURE_ERROR=%s: %s\n",
                     scenarioName.c_str(), reason.c_str());
        std::fflush(stderr);
        return 2;
    };
    if (!descriptor) return fail("unknown scenario");
    if (!m_projectManager || !m_projectSessionIntegration || !m_libraryManager ||
        !m_projectSession || !m_projectWorkshopController || !m_libraryWorkflow ||
        !m_uiManager || !m_cncController) {
        return fail("application services are incomplete");
    }
    if (!m_cncController->isSimulating()) {
        return fail("Virtual CNC is not active");
    }

    m_running = true;
    auto frame = [this]() {
        processEvents();
        update();
        render();
        SDL_Delay(8);
    };
    auto waitFor = [&frame](const std::function<bool()>& predicate,
                            int maxFrames = 500) {
        for (int index = 0; index < maxFrames; ++index) {
            frame();
            if (predicate()) return true;
        }
        return false;
    };
    for (int index = 0; index < 3; ++index) frame();

    std::string fixtureError;
    const auto fixture = seedUxCaptureFixture(
        *m_libraryManager, *m_projectManager,
        *m_projectSessionIntegration, fixtureError);
    if (!fixture) return fail(fixtureError);
    m_projectWorkshopController->setGuidedEnabled(true);
    const auto& presets = Config::instance().getLayoutPresets();
    const auto guided = std::find_if(presets.begin(), presets.end(), isGuidedLayout);
    if (guided == presets.end()) return fail("Guided Workshop layout is missing");
    m_uiManager->applyLayoutPreset(
        static_cast<int>(std::distance(presets.begin(), guided)));

    std::string selectedDesign;
    auto showProject = [this]() {
        m_uiManager->setWorkspaceMode(WorkspaceMode::Model);
        m_uiManager->showStartPage() = false;
        m_uiManager->showLibrary() = false;
        m_uiManager->showProject() = true;
        m_uiManager->showViewport() = true;
        m_uiManager->showProperties() = false;
        m_uiManager->openWindow("project");
    };

    using design_library::LibraryPickerPurpose;
    using design_library::ReplaceLibrarySelection;
    using design_library::RequestLibraryPreview;
    using design_library::CompleteLibraryPreview;
    const auto scenario = descriptor->scenario;
    if (scenario == UxCaptureScenario::GuidedHome) {
        showHome();
    } else if (scenario == UxCaptureScenario::LibraryStartProject) {
        const auto closed = m_projectSessionIntegration->closeProject();
        if (!closed.committed()) return fail("active project could not close");
        showHome();
        if (!showDesignLibrary(LibraryPickerPurpose::StartProject))
            return fail("Start Project picker could not open");
        auto selected = m_libraryWorkflow->picker().dispatch(
            ReplaceLibrarySelection{{fixture->primary}},
            m_projectSession->snapshot());
        if (selected.status !=
            design_library::LibraryPickerTransitionStatus::Applied) {
            return fail("Primary could not be selected in Start Project");
        }
        m_uiManager->libraryPanel()->setPickerState(selected.snapshot);
        m_uiManager->libraryPanel()->setSelectedModelId(
            fixture->primary.item.value);
        selectedDesign = fixture->primaryName;
    } else if (scenario == UxCaptureScenario::LibraryPreview) {
        if (!showDesignLibrary(LibraryPickerPurpose::AddToProject))
            return fail("Choose Model picker could not open");
        auto selected = m_libraryWorkflow->picker().dispatch(
            ReplaceLibrarySelection{{fixture->previewOnly}},
            m_projectSession->snapshot());
        if (selected.status !=
            design_library::LibraryPickerTransitionStatus::Applied) {
            return fail("Preview Only could not be selected");
        }
        auto issued = m_libraryWorkflow->picker().dispatch(
            RequestLibraryPreview{fixture->previewOnly},
            m_projectSession->snapshot());
        const auto* request = issued.request
            ? std::get_if<design_library::PreviewLibraryItemRequest>(
                  &*issued.request)
            : nullptr;
        if (!request) return fail("preview request was not issued");
        auto mesh = m_libraryManager->loadMesh(
            fixture->previewOnly.item.value);
        if (!mesh || !mesh->isValid())
            return fail("Preview Only mesh could not load");
        m_workspace->setFocusedMesh(mesh);
        m_uiManager->viewportPanel()->setPreOrientedMesh(mesh, 0.0f);
        m_uiManager->viewportPanel()->setPresentationIdentity(
            viewport::PresentationIdentity::libraryPreview(
                fixture->projectName, fixture->previewName));
        m_uiManager->propertiesPanel()->setMesh(mesh, fixture->previewName);
        const auto previewed = m_projectWorkshopController->dispatch(
            workshop::PreviewLibraryItemIntent{
                workshop::ExperienceMode::Guided,
                m_projectSession->snapshot().generation,
                fixture->previewOnly});
        if (!previewed.accepted()) return fail("preview route was rejected");
        auto completed = m_libraryWorkflow->picker().dispatch(
            CompleteLibraryPreview{request->token, true},
            m_projectSession->snapshot());
        if (!completed.snapshot.previewItem)
            return fail("preview completion lost identity");
        m_uiManager->libraryPanel()->setPickerState(completed.snapshot);
        m_uiManager->libraryPanel()->setSelectedModelId(
            fixture->previewOnly.item.value);
        selectedDesign = fixture->previewName;
    } else if (scenario == UxCaptureScenario::ProjectPlan) {
        showProject();
        const auto item = m_projectManager->findOpenItem(
            fixture->primaryProjectItem.item.value);
        if (!item ||
            activateProjectOpenItem(*item, false) ==
                ProjectItemActivationStatus::Rejected) {
            return fail("Primary project item could not activate");
        }
        if (!waitFor([this, &fixture]() {
                const auto context = m_projectSession->snapshot();
                return !m_loadingState.active.load() &&
                       context.activeProjectItem ==
                           fixture->primaryProjectItem;
            })) {
            return fail("Primary project item activation timed out");
        }
        selectedDesign = fixture->primaryName;
    } else if (isDirectScenario(scenario)) {
        showProject();
        if (!beginPrepareCarve(fixture->primaryProjectItem))
            return fail("pinned preparation could not begin");
        if (!waitFor([this]() {
                const auto* panel = m_uiManager->directCarvePanel();
                return !m_loadingState.active.load() && panel &&
                       panel->preparationPin().has_value();
            })) {
            return fail("pinned preparation load timed out");
        }
        auto* panel = m_uiManager->directCarvePanel();
        if (!panel->stageUxCaptureState(directState(scenario)))
            return fail("Direct Carve scenario could not reach a truthful state");
        selectedDesign = panel->uxCaptureSnapshot().design;
        m_uiManager->openWindow("direct_carve");
    }

    auto expectedRoute = workshop::WorkshopRoute::Project;
    if (scenario == UxCaptureScenario::GuidedHome)
        expectedRoute = workshop::WorkshopRoute::Home;
    else if (scenario == UxCaptureScenario::LibraryStartProject ||
             scenario == UxCaptureScenario::LibraryPreview)
        expectedRoute = workshop::WorkshopRoute::DesignLibrary;
    else if (scenario == UxCaptureScenario::RunStreaming ||
             scenario == UxCaptureScenario::RunPausedAbortFocused)
        expectedRoute = workshop::WorkshopRoute::RunCnc;

    const auto assertScenario = [this, &fixture, scenario, expectedRoute]() {
        const auto context = m_projectSession->snapshot();
        if (context.route != expectedRoute) return false;
        const auto& picker = m_libraryWorkflow->picker().snapshot();
        if (scenario == UxCaptureScenario::LibraryStartProject) {
            return !context.activeProject.has_value() && picker.active &&
                   picker.purpose == LibraryPickerPurpose::StartProject &&
                   picker.selectedItems.size() == 1 &&
                   sameLibraryItem(picker.selectedItems.front(), fixture->primary) &&
                   picker.projectMembership.empty();
        }
        if (!context.activeProject || *context.activeProject != fixture->project)
            return false;
        if (scenario == UxCaptureScenario::GuidedHome) {
            const auto& recent = Config::instance().getRecentProjects();
            return std::find(recent.begin(), recent.end(), fixture->projectRoot) !=
                   recent.end();
        }
        if (scenario == UxCaptureScenario::LibraryPreview) {
            const auto member = [&picker](workshop::LibraryItemRef item) {
                return std::any_of(
                    picker.projectMembership.begin(),
                    picker.projectMembership.end(),
                    [item](workshop::LibraryItemRef candidate) {
                        return sameLibraryItem(candidate, item);
                    });
            };
            return context.libraryPreview.has_value() &&
                   context.libraryPreview->item == fixture->previewOnly.item &&
                   context.libraryReturnRoute == workshop::WorkshopRoute::Project &&
                   picker.active &&
                   picker.purpose == LibraryPickerPurpose::AddToProject &&
                   picker.returnRoute == workshop::WorkshopRoute::Project &&
                   picker.previewItem &&
                   sameLibraryItem(*picker.previewItem, fixture->previewOnly) &&
                   member(fixture->primary) && !member(fixture->alternate) &&
                   !member(fixture->previewOnly);
        }
        if (scenario == UxCaptureScenario::ProjectPlan)
            return context.activeProjectItem ==
                   fixture->primaryProjectItem;
        if (isDirectScenario(scenario)) {
            const auto* panel = m_uiManager->directCarvePanel();
            if (!panel || !panel->preparationPin()) return false;
            const auto snapshot = panel->uxCaptureSnapshot();
            if (!snapshot.previewReady &&
                static_cast<int>(scenario) >=
                    static_cast<int>(UxCaptureScenario::PrepareCarvePreview)) {
                return false;
            }
            if (scenario == UxCaptureScenario::ReviewMissingRequirement &&
                snapshot.startAvailable) {
                return false;
            }
            if (static_cast<int>(scenario) >=
                    static_cast<int>(UxCaptureScenario::ReviewReady) &&
                static_cast<int>(scenario) <
                    static_cast<int>(UxCaptureScenario::RunStreaming) &&
                !snapshot.startAvailable) {
                return false;
            }
            if (scenario == UxCaptureScenario::RunStreaming ||
                scenario == UxCaptureScenario::RunPausedAbortFocused) {
                const auto project = m_uiManager->projectPanel()->uxCaptureSnapshot();
                if (!project || project->plan.nextAction.kind !=
                                    project_plan::NextActionKind::MonitorRun) {
                    return false;
                }
            }
        }
        return true;
    };
    if (!waitFor(assertScenario, 120))
        return fail("scenario assertions did not settle");

    if (scenario == UxCaptureScenario::LibraryPreview ||
        scenario == UxCaptureScenario::ProjectPlan) {
        // Docking and the content panel must settle before the production Fit
        // action measures its actual viewport. This keeps deterministic
        // captures representative at both compact and 4K work areas.
        for (int index = 0; index < 4; ++index) frame();
        m_uiManager->viewportPanel()->fitToModel();
        for (int index = 0; index < 8; ++index) frame();
    }

    std::string focusedControl;
    if (scenario == UxCaptureScenario::RunPausedAbortFocused) {
        auto* panel = m_uiManager->directCarvePanel();
        panel->primeUxCaptureAbortFocus();
        if (!waitFor([panel]() {
                return panel->uxCaptureSnapshot().focusPrimed;
            }, 120)) {
            return fail("run focus could not be primed");
        }
        std::printf("DW_UX_CAPTURE_AWAIT_FOCUS=Hold to Abort\n");
        std::fflush(stdout);
        if (!waitFor([panel]() {
                return panel->uxCaptureSnapshot().abortFocused;
            }, 600)) {
            return fail("Hold to Abort did not receive keyboard focus");
        }
        focusedControl = "Hold to Abort";
    } else {
        for (int index = 0; index < 8; ++index) frame();
    }

    const auto context = m_projectSession->snapshot();
    const auto project = m_projectManager->currentProject();
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(m_window, &width, &height);
    nlohmann::json ready = {
        {"scenario", descriptor->name},
        {"ordinal", descriptor->ordinal},
        {"surface", descriptor->surface},
        {"build_id", GIT_HASH},
        {"version", VERSION},
        {"actual_width", width},
        {"actual_height", height},
        {"ui_scale", Config::instance().getUiScale()},
        {"route", routeName(context.route)},
        {"project", project ? project->name() : std::string()},
        {"selected_design", selectedDesign},
        {"keyboard_focused_control", focusedControl},
        {"virtual_cnc", m_cncController->isSimulating()},
    };
    if (isDirectScenario(scenario)) {
        ready["direct_stage"] =
            m_uiManager->directCarvePanel()->uxCaptureSnapshot().stage;
    }
    m_uxCaptureWriteComplete = false;
    m_uxCaptureWriteSucceeded = false;
    m_uxCaptureWriteError.clear();
    m_pendingUxCaptureOutput = outputPath;
    frame();
    if (!m_uxCaptureWriteComplete || !m_uxCaptureWriteSucceeded)
        return fail(m_uxCaptureWriteError.empty()
                        ? "capture framebuffer was not written"
                        : m_uxCaptureWriteError);
    std::printf("DW_UX_CAPTURE_READY=%s\n", ready.dump().c_str());
    std::fflush(stdout);

    // Leave the last fully swapped frame unchanged while the external X11
    // capturer reads it. Rendering concurrently with ImageMagick under Xvfb
    // can expose a partially cleared front buffer at high UI scales.
    const auto hold = std::clamp(holdMilliseconds, 1000, 60000);
    const Uint64 deadline = SDL_GetTicks64() + static_cast<Uint64>(hold);
    while (m_running && SDL_GetTicks64() < deadline) {
        processEvents();
        SDL_Delay(8);
    }
    m_running = false;
    return 0;
}

} // namespace dw

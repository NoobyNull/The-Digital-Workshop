// Explicit facilitator entry point for the River Sign novice study. Normal
// launches never call this path, and this path never performs participant work.

#include "app/application.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "app/library_workflow_coordinator.h"
#include "app/river_sign_study_fixture.h"
#include "core/cnc/cnc_controller.h"
#include "core/config/config.h"
#include "core/config/layout_migration.h"
#include "core/database/gcode_repository.h"
#include "core/library/library_manager.h"
#include "core/project/project.h"
#include "managers/ui_manager.h"
#include "modules/design_library/library_picker_flow.h"
#include "modules/project_session/project_session.h"
#include "modules/workshop/project_workshop_controller.h"
#include "version.h"

namespace dw {

int Application::runRiverSignStudy(const Path& fixtureDirectory) {
    auto fail = [](const std::string& reason) {
        std::fprintf(stderr, "DW_STUDY_ERROR=%s\n", reason.c_str());
        std::fflush(stderr);
        return 2;
    };
    if (!m_initialized || !m_projectManager || !m_libraryManager ||
        !m_gcodeRepo || !m_projectSession || !m_projectWorkshopController ||
        !m_libraryWorkflow || !m_uiManager || !m_cncController) {
        return fail("application services are incomplete");
    }

    const auto initialContext = m_projectSession->snapshot();
    const auto initialPicker = m_libraryWorkflow->picker().snapshot();
    if (!m_projectManager->listProjects().empty() ||
        m_projectManager->currentProject() || initialContext.activeProject ||
        initialContext.activeProjectItem) {
        return fail("study profile is not fresh: a project already exists or is active");
    }
    if (m_libraryManager->modelCount() != 0 || m_gcodeRepo->count() != 0) {
        return fail("study profile is not fresh: the Design Library is not empty");
    }
    if (initialPicker.active) {
        return fail("study profile is not fresh: a Library task is already active");
    }

    const auto seeded =
        river_sign_study::seedLibraryFixture(*m_libraryManager, fixtureDirectory);
    if (!seeded.seeded())
        return fail(seeded.error);

    const auto& presets = Config::instance().getLayoutPresets();
    const auto guided = std::find_if(presets.begin(), presets.end(), isGuidedLayout);
    if (guided == presets.end())
        return fail("Guided Workshop layout is missing");
    const int guidedIndex = static_cast<int>(std::distance(presets.begin(), guided));
    m_projectWorkshopController->setGuidedEnabled(true);
    m_uiManager->applyLayoutPreset(guidedIndex);
    if (!m_projectWorkshopController->guidedEnabled() ||
        Config::instance().getActiveLayoutPresetIndex() != guidedIndex) {
        return fail("Guided Workshop could not be selected");
    }

    if (!showDesignLibrary(design_library::LibraryPickerPurpose::StartProject))
        return fail("Start Project Library could not be opened");

    const auto context = m_projectSession->snapshot();
    const auto picker = m_libraryWorkflow->picker().snapshot();
    auto models = m_libraryManager->getAllModels();
    std::vector<std::string> actualNames;
    actualNames.reserve(models.size());
    for (const auto& model : models)
        actualNames.push_back(model.name);
    std::sort(actualNames.begin(), actualNames.end());
    const std::vector<std::string> expectedNames{
        "Alternate", "Preview Only", "Primary"};

    if (!m_projectManager->listProjects().empty() ||
        m_projectManager->currentProject() || context.activeProject ||
        context.activeProjectItem) {
        return fail("study preparation created or activated a project");
    }
    if (m_libraryManager->modelCount() != 3 || models.size() != 3 ||
        actualNames != expectedNames || m_gcodeRepo->count() != 0) {
        return fail("study Library is not exactly the three named fixture designs");
    }
    if (context.route != workshop::WorkshopRoute::DesignLibrary ||
        !picker.active ||
        picker.purpose != design_library::LibraryPickerPurpose::StartProject ||
        picker.activeProject || !picker.activeProjectName.empty() ||
        !picker.projectMembership.empty() ||
        !picker.selectedItems.empty() || picker.previewItem ||
        picker.pendingPreviewItem || picker.pendingPreviewToken ||
        !picker.pendingAddItems.empty() || picker.pendingActionToken ||
        picker.startRequestPending || picker.returnPending ||
        picker.pendingRestoreToken) {
        return fail("study Library did not open in an empty Start Project state");
    }
    if (!m_cncController->isSimulating())
        return fail("Virtual CNC is not active");

    const nlohmann::json ready = {
        {"version", VERSION},
        {"git_hash", GIT_HASH},
        {"route", "Design Library"},
        {"model_names", seeded.fixture.modelNames()},
        {"model_count", models.size()},
        {"no_project", true},
        {"virtual_cnc", true},
        {"ui_scale", Config::instance().getUiScale()},
        {"picker_purpose", "StartProject"},
        {"selection_count", picker.selectedItems.size()},
        {"membership_count", picker.projectMembership.size()},
    };
    std::printf("DW_STUDY_READY=%s\n", ready.dump().c_str());
    std::fflush(stdout);

    return run();
}

} // namespace dw

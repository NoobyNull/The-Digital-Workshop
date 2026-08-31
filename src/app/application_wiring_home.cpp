// Home composition. The legacy StartPage class name preserves saved layout
// identity; this unit owns the single New/Open/Recent project entry surface.

#include "app/application.h"

#include <string>
#include <utility>

#include "managers/file_io_manager.h"
#include "managers/ui_manager.h"
#include "modules/design_library/library_picker_flow.h"
#include "ui/panels/start_page.h"

namespace dw {

void Application::wireStartPage() {
    auto* home = m_uiManager ? m_uiManager->startPage() : nullptr;
    if (!home)
        return;

    auto finishProjectAction = [this](bool activated) {
        if (!activated)
            return;
        m_uiManager->setWorkspaceMode(WorkspaceMode::Model);
        m_uiManager->showStartPage() = false;
        m_uiManager->openWindow("project");
    };
    home->setOnNewProject(
        [this, finishProjectAction](std::string name,
                                    StartPage::ProjectCreationCompletion completion) {
            m_fileIOManager->newProject(
                std::move(name),
                [finishProjectAction, completion = std::move(completion)](bool created) mutable {
                    finishProjectAction(created);
                    completion(created,
                               created ? std::string{}
                                       : "The project could not be activated. Try again.");
                });
        });
    home->setOnOpenProject([this, finishProjectAction]() {
        m_fileIOManager->openProject(finishProjectAction);
    });
    home->setOnOpenRecentProject([this, finishProjectAction](const Path& path) {
        m_fileIOManager->openRecentProject(path, finishProjectAction);
    });
    home->setOnBrowseLibrary([this]() {
        (void)showDesignLibrary(design_library::LibraryPickerPurpose::StartProject);
    });
    home->setOnImportModel([this]() {
        m_pendingImportLibraryPurpose = design_library::LibraryPickerPurpose::StartProject;
        m_fileIOManager->importModel();
    });
    home->setOnImportFolder([this]() {
        m_pendingImportLibraryPurpose = design_library::LibraryPickerPurpose::StartProject;
        m_fileIOManager->importFolder();
    });
}

} // namespace dw

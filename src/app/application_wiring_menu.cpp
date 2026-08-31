// Global menu/shortcut composition extracted from the main wiring unit.

#include "app/application.h"
#include "app/library_workflow_coordinator.h"

#include "managers/config_manager.h"
#include "managers/file_io_manager.h"
#include "managers/ui_manager.h"
#include "modules/design_library/library_picker_flow.h"
#include "modules/workshop/project_resume.h"
#include "ui/dialogs/tagger_shutdown_dialog.h"

namespace dw {

void Application::wireMenuActions() {
    auto finishProjectAction = [this](bool activated) {
        if (!activated)
            return;
        m_uiManager->setWorkspaceMode(WorkspaceMode::Model);
        m_uiManager->showStartPage() = false;
        m_uiManager->openWindow("project");
    };
    m_uiManager->setOnNewProject([this]() { showHome(true); });
    m_uiManager->setOnOpenProject([this, finishProjectAction]() {
        if (m_libraryWorkflow) {
            std::string error;
            if (!requestLibraryReturn(error))
                return;
        }
        m_fileIOManager->openProject(finishProjectAction);
    });
    m_uiManager->setOnSaveProject([this]() { (void)m_fileIOManager->saveProject(); });
    m_uiManager->setOnImportModel([this]() {
        const auto picker = m_libraryWorkflow ? m_libraryWorkflow->picker().snapshot()
                                              : design_library::LibraryPickerSnapshot{};
        m_pendingImportLibraryPurpose =
            picker.active ? picker.purpose
                          : design_library::LibraryPickerPurpose::ManageLibrary;
        m_fileIOManager->importModel();
    });
    m_uiManager->setOnImportFolder([this]() {
        const auto picker = m_libraryWorkflow ? m_libraryWorkflow->picker().snapshot()
                                              : design_library::LibraryPickerSnapshot{};
        m_pendingImportLibraryPurpose =
            picker.active ? picker.purpose
                          : design_library::LibraryPickerPurpose::ManageLibrary;
        m_fileIOManager->importFolder();
    });
    m_uiManager->setOnExportModel([this]() { m_fileIOManager->exportModel(); });
    m_uiManager->setOnImportProjectArchive([this, finishProjectAction]() {
        if (m_libraryWorkflow) {
            std::string error;
            if (!requestLibraryReturn(error))
                return;
        }
        m_fileIOManager->importProjectArchive(finishProjectAction);
    });
    m_uiManager->setOnOpenDesignLibrary([this]() { (void)showDesignLibrary(); });
    m_uiManager->setOnCloseDesignLibrary([this]() {
        std::string error;
        (void)requestLibraryReturn(error);
    });
    m_uiManager->setOnQuit([this]() { quit(); });
    m_uiManager->setOnSpawnSettings([this]() { m_configManager->spawnSettingsApp(); });
    m_uiManager->setOnResetToDefaults([this]() { return handleResetToDefaults(); });

    wireToolsMenu();

    if (m_uiManager->taggerShutdownDialog()) {
        m_uiManager->taggerShutdownDialog()->setOnQuit([this]() {
            requestProjectClose(workshop::ProjectClosePurpose::ApplicationExit,
                                [this](bool closed) {
                                    if (closed)
                                        m_running = false;
                                });
        });
    }
}

} // namespace dw

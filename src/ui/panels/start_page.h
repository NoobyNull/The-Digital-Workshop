#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../../core/types.h"
#include "../../modules/workshop/home_flow.h"
#include "panel.h"

namespace dw {

// Home is the single project entry surface. The historical class/file name is
// retained until the versioned layout migration so saved ImGui window identity
// remains stable.
class StartPage : public Panel {
  public:
    StartPage();
    ~StartPage() override = default;

    void render() override;
    void beginNamedProject();
    void requestFocus() {
        m_focusHome = true;
        m_recentPaths.clear();
    }

    // Callbacks
    using VoidCallback = std::function<void()>;
    using PathCallback = std::function<void(const Path&)>;
    using ProjectCreationCompletion = std::function<void(bool, std::string)>;
    using NamedProjectCallback =
        std::function<void(std::string, ProjectCreationCompletion)>;

    void setOnNewProject(NamedProjectCallback cb) { m_onNewProject = std::move(cb); }
    void setOnOpenProject(VoidCallback cb) { m_onOpenProject = std::move(cb); }
    void setOnImportModel(VoidCallback cb) { m_onImportModel = std::move(cb); }
    void setOnImportFolder(VoidCallback cb) { m_onImportFolder = std::move(cb); }
    void setOnBrowseLibrary(VoidCallback cb) { m_onBrowseLibrary = std::move(cb); }
    void setOnOpenRecentProject(PathCallback cb) { m_onOpenRecentProject = std::move(cb); }

  private:
    void renderRecentProjects();
    void renderQuickActions();
    void renderNamedProjectDialog();

    NamedProjectCallback m_onNewProject;
    VoidCallback m_onOpenProject;
    VoidCallback m_onImportModel;
    VoidCallback m_onImportFolder;
    VoidCallback m_onBrowseLibrary;
    PathCallback m_onOpenRecentProject;

    workshop::HomeFlow m_homeFlow;
    char m_projectName[128]{};
    bool m_openProjectNamePopup = false;
    bool m_focusProjectName = false;
    bool m_focusHome = false;

    // Cached recent project display names (resolved from project.json)
    std::vector<std::string> m_recentNames;
    std::vector<Path> m_recentPaths;
    void refreshRecentNames();
};

} // namespace dw

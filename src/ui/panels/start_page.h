#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../../core/types.h"
#include "panel.h"

namespace dw {

// Start page shown on application launch with recent projects and quick actions.
// Renders as a non-dockable floating window — either open or closed.
class StartPage : public Panel {
  public:
    StartPage();
    ~StartPage() override = default;

    void render() override;

    // Callbacks
    using VoidCallback = std::function<void()>;
    using PathCallback = std::function<void(const Path&)>;
    using WorkspaceModeCallback = std::function<void(int)>; // 0=Model, 1=CNC

    void setOnNewProject(VoidCallback cb) { m_onNewProject = std::move(cb); }
    void setOnOpenProject(VoidCallback cb) { m_onOpenProject = std::move(cb); }
    void setOnImportModel(VoidCallback cb) { m_onImportModel = std::move(cb); }
    void setOnImportFolder(VoidCallback cb) { m_onImportFolder = std::move(cb); }
    void setOnOpenRecentProject(PathCallback cb) { m_onOpenRecentProject = std::move(cb); }
    void setOnWorkspaceModeChanged(WorkspaceModeCallback cb) { m_onWorkspaceModeChanged = std::move(cb); }

  private:
    void renderRecentProjects();
    void renderQuickActions();
    void renderSetup();
    void browseWorkspaceRoot();
    void setWorkspaceRoot(const Path& path);

    VoidCallback m_onNewProject;
    VoidCallback m_onOpenProject;
    VoidCallback m_onImportModel;
    VoidCallback m_onImportFolder;
    PathCallback m_onOpenRecentProject;
    WorkspaceModeCallback m_onWorkspaceModeChanged;

    // Local edit buffer for workspace root
    char m_workspaceRoot[512]{};
    bool m_workspaceRootLoaded = false;
    int m_startMode = 0; // 0=Modeling, 1=CNC Sender

    // Cached recent project display names (resolved from project.json)
    std::vector<std::string> m_recentNames;
    size_t m_recentNamesCount = 0; // tracks staleness
    void refreshRecentNames();
};

} // namespace dw

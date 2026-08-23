#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "modules/project_plan/project_plan.h"
#include "panel.h"

namespace dw {

struct ProjectPanelSnapshot {
    std::int64_t projectId = 0;
    std::string projectName;
    std::string description;
    std::string notes;
    bool modified = false;
    bool hasModels = false;
    project_plan::ProjectPlan plan;
    std::optional<workshop::ProjectItemId> activeItem;
};

// Project is the persistent workshop context. This panel renders one Project
// Plan snapshot and emits intents; it does not query asset repositories or own
// preparation policy.
class ProjectPanel : public Panel {
  public:
    ProjectPanel();
    ~ProjectPanel() override = default;

    void render() override;
    void clearSelection() {
        m_notesProjectId = -1;
        m_notesChanged = false;
    }

    using ProjectPlanProvider = std::function<std::optional<ProjectPanelSnapshot>()>;
    using ProjectPlanActionCallback =
        std::function<void(const project_plan::NextAction&)>;
    using ProjectItemActivatedCallback =
        std::function<void(workshop::ProjectItemRef)>;

    void setProjectPlanProvider(ProjectPlanProvider provider) {
        m_projectPlanProvider = std::move(provider);
    }
#ifdef DW_ENABLE_UX_CAPTURE
    [[nodiscard]] std::optional<ProjectPanelSnapshot> uxCaptureSnapshot() const {
        return m_projectPlanProvider ? m_projectPlanProvider() : std::nullopt;
    }
#endif
    void setOnProjectPlanAction(ProjectPlanActionCallback callback) {
        m_onProjectPlanAction = std::move(callback);
    }
    void setOnProjectItemActivated(ProjectItemActivatedCallback callback) {
        m_onProjectItemActivated = std::move(callback);
    }

    using OpenHomeCallback = std::function<void()>;
    void setOpenHomeCallback(OpenHomeCallback callback) {
        m_openHomeCallback = std::move(callback);
    }

    using CloseProjectCallback = std::function<void()>;
    void setCloseProjectCallback(CloseProjectCallback callback) {
        m_closeProjectCallback = std::move(callback);
    }

    using SaveProjectCallback = std::function<bool()>;
    void setSaveProjectCallback(SaveProjectCallback callback) {
        m_saveProjectCallback = std::move(callback);
    }

    using ExportProjectCallback = std::function<void()>;
    void setExportProjectCallback(ExportProjectCallback callback) {
        m_exportProjectCallback = std::move(callback);
    }

    using UpdateNotesCallback =
        std::function<void(std::int64_t projectId, std::string notes)>;
    void setUpdateNotesCallback(UpdateNotesCallback callback) {
        m_updateNotesCallback = std::move(callback);
    }

  private:
    void renderProjectHeader(const ProjectPanelSnapshot& snapshot);
    void renderProjectPlan(const ProjectPanelSnapshot& snapshot);
    void renderProjectDetails(const ProjectPanelSnapshot& snapshot);
    void renderNoProject();

    ProjectPlanProvider m_projectPlanProvider;
    ProjectPlanActionCallback m_onProjectPlanAction;
    ProjectItemActivatedCallback m_onProjectItemActivated;
    OpenHomeCallback m_openHomeCallback;
    CloseProjectCallback m_closeProjectCallback;
    SaveProjectCallback m_saveProjectCallback;
    ExportProjectCallback m_exportProjectCallback;
    UpdateNotesCallback m_updateNotesCallback;

    char m_notesBuf[4096] = {};
    bool m_notesChanged = false;
    std::int64_t m_notesProjectId = -1;
};

} // namespace dw

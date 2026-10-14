#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "panel.h"
#include "project_contents.h"

namespace dw {

struct ProjectPanelSnapshot {
    std::int64_t projectId = 0;
    std::string projectName;
    std::string description;
    std::string notes;
    bool modified = false;
    bool hasModels = false;
    ProjectContents contents;
    std::optional<workshop::ProjectItemId> activeItem;
};

// Project is the persistent workshop context, organized like a slicer project:
// designs, the CAM setups made from them, and the G-code they produced. This
// panel renders one snapshot and emits intents; it does not query repositories.
class ProjectPanel : public Panel {
  public:
    ProjectPanel();
    ~ProjectPanel() override = default;

    void render() override;
    void clearSelection() {
        m_notesProjectId = -1;
        m_notesChanged = false;
    }

    using ProjectSnapshotProvider = std::function<std::optional<ProjectPanelSnapshot>()>;
    using ProjectItemCallback = std::function<void(workshop::ProjectItemRef)>;

    void setProjectSnapshotProvider(ProjectSnapshotProvider provider) {
        m_snapshotProvider = std::move(provider);
    }
    void setOnProjectItemActivated(ProjectItemCallback callback) {
        m_onProjectItemActivated = std::move(callback);
    }
    // Opens the design's CAM setup, creating one when the design has none.
    void setOnOpenInCam(ProjectItemCallback callback) {
        m_onOpenInCam = std::move(callback);
    }

    using AddFromLibraryCallback = std::function<void()>;
    void setAddFromLibraryCallback(AddFromLibraryCallback callback) {
        m_addFromLibraryCallback = std::move(callback);
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
    void renderProjectContents(const ProjectPanelSnapshot& snapshot);
    void renderContentRow(const ProjectPanelSnapshot& snapshot, std::size_t index);
    void renderProjectDetails(const ProjectPanelSnapshot& snapshot);
    void renderNoProject();

    ProjectSnapshotProvider m_snapshotProvider;
    ProjectItemCallback m_onProjectItemActivated;
    ProjectItemCallback m_onOpenInCam;
    AddFromLibraryCallback m_addFromLibraryCallback;
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

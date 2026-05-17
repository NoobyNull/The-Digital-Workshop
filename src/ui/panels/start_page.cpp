#include "start_page.h"

#include <cstring>
#include <filesystem>
#include <fstream>

#include <imgui.h>
#include <nfd.h>
#include <nlohmann/json.hpp>

#include "../../core/config/config.h"
#include "../../core/paths/app_paths.h"
#include "../../core/utils/file_utils.h"
#include "version.h"

namespace dw {

StartPage::StartPage() : Panel("Start Page") {}

void StartPage::render() {
    if (!m_open)
        return;

    // Load workspace root from config on first render
    if (!m_workspaceRootLoaded) {
        auto root = Config::instance().getWorkspaceRoot();
        std::strncpy(m_workspaceRoot, root.string().c_str(), sizeof(m_workspaceRoot) - 1);
        m_workspaceRoot[sizeof(m_workspaceRoot) - 1] = '\0';
        m_startMode = Config::instance().getActiveLayoutPresetIndex();
        m_workspaceRootLoaded = true;
    }

    // Non-dockable floating window
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;

    const auto* viewport = ImGui::GetMainViewport();
    ImVec2 startSize(viewport->WorkSize.x * 0.55f, viewport->WorkSize.y * 0.55f);
    ImGui::SetNextWindowSize(startSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
               viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
        ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    applyMinSize(30, 16);
    if (ImGui::Begin(m_title.c_str(), &m_open, flags)) {
        // Header
        ImGui::Text("Digital Workshop");
        ImGui::SameLine();
        ImGui::TextDisabled("v%s", VERSION);
        ImGui::TextDisabled("3D Model Management for CNC and 3D Printing");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Reserve space for bottom checkbox
        float checkboxHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        float contentHeight = ImGui::GetContentRegionAvail().y - checkboxHeight;
        float availWidth = ImGui::GetContentRegionAvail().x;
        float leftWidth = availWidth * 0.55f;

        // Left column: setup + recent projects
        ImGui::BeginChild("##StartLeft", ImVec2(leftWidth, contentHeight), false);
        renderSetup();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        renderRecentProjects();
        ImGui::EndChild();

        ImGui::SameLine();

        // Right column: quick actions
        ImGui::BeginChild("##StartRight", ImVec2(0, contentHeight), false);
        renderQuickActions();
        ImGui::EndChild();

        // Show at launch checkbox
        ImGui::Spacing();
        bool showAtLaunch = Config::instance().getShowStartPage();
        if (ImGui::Checkbox("Show at launch", &showAtLaunch)) {
            Config::instance().setShowStartPage(showAtLaunch);
            Config::instance().save();
        }
    }
    ImGui::End();
}

void StartPage::setWorkspaceRoot(const Path& path) {
    const std::string value = path.string();
    std::strncpy(m_workspaceRoot, value.c_str(), sizeof(m_workspaceRoot) - 1);
    m_workspaceRoot[sizeof(m_workspaceRoot) - 1] = '\0';

    Config::instance().setWorkspaceRoot(path);
    Config::instance().save();
}

void StartPage::browseWorkspaceRoot() {
    Path startPath(m_workspaceRoot);
    if (startPath.empty() || !std::filesystem::is_directory(startPath)) {
        startPath = paths::getUserRoot().parent_path();
    }
    if (startPath.empty() || !std::filesystem::is_directory(startPath)) {
        startPath = std::filesystem::current_path();
    }

    NFD_Init();
    nfdchar_t* outPath = nullptr;
    nfdresult_t result = NFD_PickFolder(&outPath, startPath.string().c_str());
    if (result == NFD_OKAY && outPath) {
        setWorkspaceRoot(Path(outPath));
        NFD_FreePath(outPath);
    } else if (result == NFD_ERROR) {
        std::fprintf(stderr, "Native folder dialog error: %s\n", NFD_GetError());
    }
    NFD_Quit();
}

void StartPage::renderSetup() {
    ImGui::Text("Setup");
    ImGui::Spacing();

    // Workspace root
    ImGui::TextDisabled("Workspace Root");
    const float browseWidth =
        ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth -
                            ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::InputText("##WorkspaceRoot", m_workspaceRoot, sizeof(m_workspaceRoot))) {
        Config::instance().setWorkspaceRoot(Path(m_workspaceRoot));
        Config::instance().save();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Base directory for all project files.\nLeave empty for default: %s",
                          paths::getUserRoot().string().c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        browseWorkspaceRoot();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Choose the workspace root with the system folder picker.");
    }

    // Show validation
    Path wsPath(m_workspaceRoot);
    if (!wsPath.empty()) {
        if (std::filesystem::exists(wsPath) && std::filesystem::is_directory(wsPath)) {
            ImGui::SameLine();
        } else {
            ImGui::TextDisabled("Directory will be created when needed.");
        }
    } else {
        ImGui::TextDisabled("Using default: %s", paths::getUserRoot().string().c_str());
    }

    ImGui::Spacing();

    // Start mode selection
    ImGui::TextDisabled("Start In");
    if (ImGui::RadioButton("Workshop / Projects", m_startMode == 0)) {
        m_startMode = 0;
        if (m_onWorkspaceModeChanged)
            m_onWorkspaceModeChanged(0);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("CNC Sender", m_startMode == 1)) {
        m_startMode = 1;
        if (m_onWorkspaceModeChanged)
            m_onWorkspaceModeChanged(1);
    }
}

void StartPage::refreshRecentNames() {
    const auto& recentProjects = Config::instance().getRecentProjects();
    m_recentNames.clear();
    m_recentNames.reserve(recentProjects.size());
    for (const auto& projectPath : recentProjects) {
        std::string name;
        Path manifestPath = projectPath / "project.json";
        if (file::exists(manifestPath)) {
            try {
                std::ifstream ifs(manifestPath.string());
                auto j = nlohmann::json::parse(ifs);
                name = j.value("name", "");
            } catch (...) {}
        }
        if (name.empty())
            name = projectPath.stem().string();
        if (name.empty())
            name = projectPath.filename().string();
        m_recentNames.push_back(std::move(name));
    }
    m_recentNamesCount = recentProjects.size();
}

void StartPage::renderRecentProjects() {
    ImGui::Text("Recent Projects");
    ImGui::Spacing();

    const auto& recentProjects = Config::instance().getRecentProjects();

    // Refresh cached names when list changes
    if (recentProjects.size() != m_recentNamesCount)
        refreshRecentNames();

    if (recentProjects.empty()) {
        ImGui::TextDisabled("No recent projects.");
        ImGui::TextDisabled("Create a new project or open an existing one.");
    } else {
        for (size_t i = 0; i < recentProjects.size(); ++i) {
            const auto& projectPath = recentProjects[i];
            ImGui::PushID(static_cast<int>(i));

            std::string name = (i < m_recentNames.size())
                                   ? m_recentNames[i]
                                   : projectPath.stem().string();

            float rowH = ImGui::GetTextLineHeightWithSpacing();
            if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_None,
                                  ImVec2(0, rowH))) {
                if (m_onOpenRecentProject) {
                    m_onOpenRecentProject(projectPath);
                }
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", projectPath.string().c_str());
            }

            ImGui::SameLine();
            ImGui::TextDisabled("%s", projectPath.parent_path().string().c_str());

            ImGui::PopID();
        }
    }
}

void StartPage::renderQuickActions() {
    ImGui::Text("Quick Actions");
    ImGui::Spacing();

    float buttonWidth = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().WindowPadding.x * 2;
    float buttonHeight = ImGui::GetFrameHeight() * 1.4f;

    if (ImGui::Button("New Project", ImVec2(buttonWidth, buttonHeight))) {
        if (m_onNewProject)
            m_onNewProject();
    }

    ImGui::Spacing();

    if (ImGui::Button("Open Project", ImVec2(buttonWidth, buttonHeight))) {
        if (m_onOpenProject)
            m_onOpenProject();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Import Models", ImVec2(buttonWidth, buttonHeight))) {
        if (m_onImportModel)
            m_onImportModel();
    }

    ImGui::Spacing();

    if (ImGui::Button("Import Folder", ImVec2(buttonWidth, buttonHeight))) {
        if (m_onImportFolder)
            m_onImportFolder();
    }
}

} // namespace dw

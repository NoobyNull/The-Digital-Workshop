#include "start_page.h"

#include <fstream>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "../../core/config/config.h"
#include "../../core/utils/file_utils.h"
#include "../../modules/workshop/ui/guided_layout_metrics.h"
#include "version.h"

namespace dw {

StartPage::StartPage() : Panel("Home###Start Page") {}

void StartPage::beginNamedProject() {
    const auto result = m_homeFlow.dispatch(workshop::BeginNamedProject{});
    if (result.snapshot.namingProject) {
        if (result.status == workshop::HomeTransitionStatus::Applied)
            m_projectName[0] = '\0';
        m_openProjectNamePopup = true;
        m_focusProjectName = true;
        m_open = true;
    }
}

void StartPage::render() {
    if (!m_open)
        return;

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (m_focusHome) {
        ImGui::SetNextWindowFocus();
        m_focusHome = false;
    }

    applyMinSize(30, 16);
    if (ImGui::Begin(m_title.c_str(), &m_open, flags)) {
        const auto homeLayout = workshop::ui::chooseGuidedHomeLayout(
            ImGui::GetContentRegionAvail().x, ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + homeLayout.horizontalOffset);
        ImGui::BeginChild(
            "##GuidedHomeContent", ImVec2(homeLayout.contentWidth, 0.0F), false);

        // Header
        ImGui::Text("Home");
        ImGui::SameLine();
        ImGui::TextDisabled("v%s", VERSION);
        ImGui::TextDisabled("Start a project or continue one you already know.");
        ImGui::TextWrapped(
            "Projects keep the design, preparation, and machine work for one job together.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float contentHeight = ImGui::GetContentRegionAvail().y;

        // Left column: recent projects
        ImGui::BeginChild(
            "##StartLeft", ImVec2(homeLayout.leftColumnWidth, contentHeight), false);
        renderRecentProjects();
        ImGui::EndChild();

        ImGui::SameLine();

        // Right column: quick actions
        ImGui::BeginChild("##StartRight", ImVec2(0, contentHeight), false);
        renderQuickActions();
        ImGui::EndChild();

        ImGui::EndChild();
        renderNamedProjectDialog();
    }
    ImGui::End();
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
    m_recentPaths = recentProjects;
}

void StartPage::renderRecentProjects() {
    ImGui::Text("Recent Projects");
    ImGui::Spacing();

    const auto& recentProjects = Config::instance().getRecentProjects();

    // Refresh cached names when list changes
    if (recentProjects != m_recentPaths)
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

            ImGui::PopID();
        }
    }
}

void StartPage::renderQuickActions() {
    ImGui::Text("Start Here");
    ImGui::Spacing();

    float buttonWidth = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().WindowPadding.x * 2;
    float buttonHeight = ImGui::GetFrameHeight() * 1.4f;

    if (ImGui::Button("New Project", ImVec2(buttonWidth, buttonHeight))) {
        beginNamedProject();
    }

    ImGui::Spacing();

    if (ImGui::Button("Open Project", ImVec2(buttonWidth, buttonHeight))) {
        if (m_onOpenProject)
            m_onOpenProject();
    }

    ImGui::Spacing();

    if (ImGui::Button("Browse Design Library", ImVec2(buttonWidth, buttonHeight))) {
        if (m_onBrowseLibrary)
            m_onBrowseLibrary();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("Import designs");

    const float importButtonWidth =
        (buttonWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
    if (ImGui::Button("Import Models", ImVec2(importButtonWidth, buttonHeight))) {
        if (m_onImportModel)
            m_onImportModel();
    }

    ImGui::SameLine();
    if (ImGui::Button("Import Folder", ImVec2(importButtonWidth, buttonHeight))) {
        if (m_onImportFolder)
            m_onImportFolder();
    }
}

void StartPage::renderNamedProjectDialog() {
    if (m_openProjectNamePopup) {
        ImGui::OpenPopup("Name Your Project");
        m_openProjectNamePopup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.0f, 0.0f),
                             ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Name Your Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const auto& before = m_homeFlow.snapshot();
    if (!before.namingProject) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextWrapped("Give this project a name you will recognize later.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Project name");
    ImGui::SetNextItemWidth(-1.0f);
    if (m_focusProjectName) {
        ImGui::SetKeyboardFocusHere();
        m_focusProjectName = false;
    }
    if (before.submittingProject)
        ImGui::BeginDisabled();
    const bool pressedEnter = ImGui::InputText("##ProjectName",
                                                m_projectName,
                                                sizeof(m_projectName),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    if (before.submittingProject)
        ImGui::EndDisabled();
    if (ImGui::IsItemEdited())
        (void)m_homeFlow.dispatch(workshop::EditProjectName{m_projectName});

    const auto& snapshot = m_homeFlow.snapshot();
    if (!snapshot.validationMessage.empty()) {
        ImGui::TextWrapped("%s", snapshot.validationMessage.c_str());
    } else {
        ImGui::TextDisabled("A project folder will be prepared after you continue.");
    }

    ImGui::Spacing();
    const bool canSubmit = snapshot.canSubmitProject();
    if (!canSubmit)
        ImGui::BeginDisabled();
    const bool createClicked =
        ImGui::Button(snapshot.submittingProject ? "Creating..." : "Create Project");
    if (!canSubmit)
        ImGui::EndDisabled();

    if ((createClicked || (pressedEnter && canSubmit)) && m_onNewProject) {
        const auto result = m_homeFlow.dispatch(workshop::SubmitNamedProject{});
        if (result.createRequest) {
            m_onNewProject(result.createRequest->name,
                           [this](bool created, std::string failureMessage) {
                               (void)m_homeFlow.dispatch(workshop::CompleteNamedProject{
                                   created, std::move(failureMessage)});
                           });
            if (!m_homeFlow.snapshot().namingProject)
                ImGui::CloseCurrentPopup();
        }
    }

    ImGui::SameLine();
    if (snapshot.submittingProject)
        ImGui::BeginDisabled();
    if (ImGui::Button("Cancel")) {
        (void)m_homeFlow.dispatch(workshop::CancelNamedProject{});
        ImGui::CloseCurrentPopup();
    }
    if (snapshot.submittingProject)
        ImGui::EndDisabled();

    ImGui::EndPopup();
}

} // namespace dw

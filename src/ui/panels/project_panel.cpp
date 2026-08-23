#include "project_panel.h"

#include <imgui.h>

#include "project_plan_view.h"

namespace dw {

ProjectPanel::ProjectPanel() : Panel("Project") {}

void ProjectPanel::render() {
    if (!m_open) return;

    applyMinSize(14, 8);
    if (ImGui::Begin(m_title.c_str(), &m_open)) {
        const auto snapshot = m_projectPlanProvider ? m_projectPlanProvider()
                                                    : std::nullopt;
        if (snapshot) {
            renderProjectHeader(*snapshot);
            renderProjectPlan(*snapshot);
            ImGui::Spacing();
            renderProjectDetails(*snapshot);
        } else {
            renderNoProject();
        }
    }
    ImGui::End();
}

void ProjectPanel::renderProjectPlan(const ProjectPanelSnapshot& snapshot) {
    ProjectPlanViewCallbacks callbacks;
    callbacks.onNextAction = m_onProjectPlanAction;
    callbacks.onActivateItem = m_onProjectItemActivated;
    renderProjectPlanView(snapshot.plan, snapshot.activeItem, callbacks);
}

} // namespace dw

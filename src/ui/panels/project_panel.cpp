#include "project_panel.h"

#include <cstring>
#include <unordered_set>

#include <imgui.h>

#include "../../core/config/config.h"
#include "../../core/database/cost_repository.h"
#include "../../core/database/cut_plan_repository.h"
#include "../../core/database/gcode_repository.h"
#include "../../core/database/model_repository.h"
#include "../icons.h"
#include "../widgets/toast.h"

namespace dw {

namespace {

const char* iconForOpenItemType(ProjectOpenItemType type) {
    switch (type) {
    case ProjectOpenItemType::Model: return Icons::Model;
    case ProjectOpenItemType::Material: return Icons::Material;
    case ProjectOpenItemType::Stock: return Icons::Material;
    case ProjectOpenItemType::Tool: return Icons::Settings;
    case ProjectOpenItemType::Operation: return Icons::Settings;
    case ProjectOpenItemType::Gcode: return Icons::GCode;
    case ProjectOpenItemType::CutPlan: return Icons::Optimizer;
    case ProjectOpenItemType::Cost: return Icons::Cost;
    case ProjectOpenItemType::Job: return Icons::Play;
    case ProjectOpenItemType::Labor: return Icons::Cost;
    case ProjectOpenItemType::Consumable: return Icons::Material;
    }
    return Icons::File;
}

const char* labelForOpenItemType(ProjectOpenItemType type) {
    switch (type) {
    case ProjectOpenItemType::Model: return "Model";
    case ProjectOpenItemType::Material: return "Material";
    case ProjectOpenItemType::Stock: return "Stock";
    case ProjectOpenItemType::Tool: return "Tool";
    case ProjectOpenItemType::Operation: return "Operation";
    case ProjectOpenItemType::Gcode: return "G-code";
    case ProjectOpenItemType::CutPlan: return "Cut plan";
    case ProjectOpenItemType::Cost: return "Cost";
    case ProjectOpenItemType::Job: return "Job";
    case ProjectOpenItemType::Labor: return "Labor";
    case ProjectOpenItemType::Consumable: return "Consumable";
    }
    return "Item";
}

const char* labelForOpenItemStatus(ProjectOpenItemStatus status) {
    switch (status) {
    case ProjectOpenItemStatus::Planned: return "planned";
    case ProjectOpenItemStatus::Ready: return "ready";
    case ProjectOpenItemStatus::Generated: return "generated";
    case ProjectOpenItemStatus::Sent: return "sent";
    case ProjectOpenItemStatus::Complete: return "complete";
    case ProjectOpenItemStatus::Stale: return "stale";
    case ProjectOpenItemStatus::Missing: return "missing";
    }
    return "planned";
}

} // namespace

ProjectPanel::ProjectPanel(ProjectManager* projectManager,
                           ModelRepository* modelRepo,
                           GCodeRepository* gcodeRepo,
                           CutPlanRepository* cutPlanRepo,
                           CostRepository* costRepo)
    : Panel("Project"),
      m_projectManager(projectManager),
      m_modelRepo(modelRepo),
      m_gcodeRepo(gcodeRepo),
      m_cutPlanRepo(cutPlanRepo),
      m_costRepo(costRepo) {}

void ProjectPanel::render() {
    if (!m_open) {
        return;
    }

    applyMinSize(14, 8);
    if (ImGui::Begin(m_title.c_str(), &m_open)) {
        if (m_projectManager && m_projectManager->currentProject()) {
            renderProjectInfo();
            ImGui::Separator();
            renderOpenItemsSection();
            renderModelsSection();
            renderGCodeSection();
            renderMaterialsSection();
            renderCostsSection();
            renderCutPlansSection();
            ImGui::Separator();
            renderNotesSection();
        } else {
            renderNoProject();
        }
    }
    ImGui::End();
}

void ProjectPanel::renderOpenItemsSection() {
    auto project = m_projectManager->currentProject();
    if (!project) return;

    auto items = m_projectManager->currentOpenItems();
    std::string header = std::string(Icons::List) + " Work Order (" +
                         std::to_string(items.size()) + ")";

    bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    if (!open) return;

    ImGui::Indent();

    if (items.empty()) {
        ImGui::TextDisabled("No linked work items");
        ImGui::Unindent();
        return;
    }

    for (const auto& item : items) {
        ImGui::PushID(static_cast<int>(item.id));
        if (item.parentItemId.has_value()) {
            ImGui::Indent(ImGui::GetFontSize());
        }

        std::string label = std::string(iconForOpenItemType(item.itemType)) + " " +
                            item.displayName + "  [" + labelForOpenItemStatus(item.status) + "]";
        bool selected = item.sourceId.has_value() &&
                        item.itemType == ProjectOpenItemType::Model &&
                        *item.sourceId == m_selectedModelId;
        if (ImGui::Selectable(label.c_str(), selected)) {
            if (item.sourceId.has_value() && item.itemType == ProjectOpenItemType::Model) {
                m_selectedModelId = *item.sourceId;
                if (m_onModelSelected) m_onModelSelected(*item.sourceId);
            } else if (item.sourceId.has_value() && item.itemType == ProjectOpenItemType::Gcode) {
                if (m_onGCodeSelected) m_onGCodeSelected(*item.sourceId);
            } else if (item.itemType == ProjectOpenItemType::Operation) {
                if (m_onOperationSelected) m_onOperationSelected(item);
            }
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s | %s",
                              labelForOpenItemType(item.itemType),
                              item.sourceTable.empty() ? "manual" : item.sourceTable.c_str());
        }

        if (item.parentItemId.has_value()) {
            ImGui::Unindent(ImGui::GetFontSize());
        }
        ImGui::PopID();
    }

    ImGui::Unindent();
}

void ProjectPanel::renderProjectInfo() {
    auto project = m_projectManager->currentProject();
    if (!project) {
        return;
    }

    if (ImGui::CollapsingHeader("Project Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        // Project name with modified indicator
        std::string nameLabel = project->name();
        if (project->isModified()) {
            nameLabel += " *";
        }
        ImGui::TextWrapped("%s %s", Icons::Project, nameLabel.c_str());

        // Description
        if (!project->description().empty()) {
            ImGui::TextWrapped("%s", project->description().c_str());
        }

        // Model count
        size_t modelCount = project->modelIds().size();
        ImGui::Text("Models: %zu", modelCount);

        ImGui::Spacing();

        // Quick actions
        if (ImGui::Button("Save")) {
            bool success = false;
            if (m_saveProjectCallback) {
                m_saveProjectCallback();
                success = true;
            } else {
                try {
                    m_projectManager->save(*project);
                    success = true;
                } catch (...) {
                    success = false;
                }
            }

            if (success) {
                ToastManager::instance().show(ToastType::Success,
                                              "Saved",
                                              "Project saved successfully");
            } else {
                ToastManager::instance().show(ToastType::Error,
                                              "Save Failed",
                                              "Could not save project");
            }
        }

        // Wrap Close button to new line if narrow
        if (ImGui::GetContentRegionAvail().x > 100.0f) {
            ImGui::SameLine();
        }

        if (ImGui::Button("Close")) {
            if (project->isModified()) {
                ImGui::OpenPopup("Unsaved Changes");
            } else {
                m_projectManager->close(*project);
                m_projectManager->setCurrentProject(nullptr);
            }
        }

        // Export .dwproj button -- only enabled when project has models
        ImGui::Spacing();
        {
            bool hasModels = !project->modelIds().empty();
            if (!hasModels)
                ImGui::BeginDisabled();
            if (ImGui::Button("Export .dwproj")) {
                if (m_exportProjectCallback) {
                    m_exportProjectCallback();
                }
            }
            if (!hasModels)
                ImGui::EndDisabled();
            if (!hasModels && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Add models to project before exporting");
            }
        }

        // Unsaved changes confirmation dialog
        if (ImGui::BeginPopupModal("Unsaved Changes", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Save changes before closing?");
            ImGui::TextDisabled("You have unsaved changes in this project.");
            ImGui::Spacing();

            float projBtnW = ImGui::CalcTextSize("Save & Close").x + ImGui::GetStyle().FramePadding.x * 4;
            if (ImGui::Button("Save & Close", ImVec2(projBtnW, 0))) {
                m_projectManager->save(*project);
                m_projectManager->close(*project);
                m_projectManager->setCurrentProject(nullptr);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(projBtnW, 0))) {
                m_projectManager->close(*project);
                m_projectManager->setCurrentProject(nullptr);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(projBtnW, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Unindent();
    }
}

void ProjectPanel::renderModelsSection() {
    auto project = m_projectManager->currentProject();
    if (!project) return;

    const auto& modelIds = project->modelIds();
    std::string header = std::string(Icons::Model) + " Models (" +
                         std::to_string(modelIds.size()) + ")";

    bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    if (!open) return;

    ImGui::Indent();

    if (modelIds.empty()) {
        ImGui::TextDisabled("No models -- add from Library panel");
    } else {
        for (int64_t modelId : modelIds) {
            ImGui::PushID(static_cast<int>(modelId));

            std::string label;
            if (m_modelRepo) {
                auto record = m_modelRepo->findById(modelId);
                if (record) {
                    label = std::string(Icons::Model) + " " + record->name;
                } else {
                    label = std::string(Icons::Model) + " Model #" + std::to_string(modelId);
                }
            } else {
                label = std::string(Icons::Model) + " Model #" + std::to_string(modelId);
            }

            bool selected = (modelId == m_selectedModelId);
            if (ImGui::Selectable(label.c_str(), selected)) {
                m_selectedModelId = modelId;
                if (m_onModelSelected) m_onModelSelected(modelId);
            }

            // Context menu: remove from project
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove from Project")) {
                    project->removeModel(modelId);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::Unindent();
}

void ProjectPanel::renderGCodeSection() {
    auto project = m_projectManager->currentProject();
    if (!project || !m_gcodeRepo) return;

    auto gcodeFiles = m_gcodeRepo->findByProject(project->id());
    std::string header = std::string(Icons::GCode) + " G-code (" +
                         std::to_string(gcodeFiles.size()) + ")";

    bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    if (!open) return;

    ImGui::Indent();

    if (gcodeFiles.empty()) {
        ImGui::TextDisabled("No G-code files -- import from G-code panel");
    } else {
        for (const auto& gc : gcodeFiles) {
            ImGui::PushID(static_cast<int>(gc.id));
            std::string label = std::string(Icons::GCode) + " " + gc.name;
            if (ImGui::Selectable(label.c_str())) {
                if (m_onGCodeSelected) m_onGCodeSelected(gc.id);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Time: %.1f min | Tools: %zu",
                    gc.estimatedTime / 60.0f, gc.toolNumbers.size());
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove from Project")) {
                    m_gcodeRepo->removeFromProject(project->id(), gc.id);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::Unindent();
}

void ProjectPanel::renderMaterialsSection() {
    auto project = m_projectManager->currentProject();
    if (!project || !m_modelRepo) return;

    // Collect unique materials from project models
    // ModelRecord doesn't have materialId/materialName, so we just show
    // a count of unique models that could have materials assigned.
    // For now, show the section as a placeholder with model-count info.
    std::string header = std::string(Icons::Material) + " Materials";

    bool open = ImGui::CollapsingHeader(header.c_str());
    if (!open) return;

    ImGui::Indent();
    ImGui::TextDisabled("Assign materials to models in the Materials panel");
    ImGui::Unindent();
}

void ProjectPanel::renderCostsSection() {
    auto project = m_projectManager->currentProject();
    if (!project || !m_costRepo) return;

    auto estimates = m_costRepo->findByProject(project->id());
    std::string header = std::string(Icons::Cost) + " Cost Estimates (" +
                         std::to_string(estimates.size()) + ")";

    bool open = ImGui::CollapsingHeader(header.c_str());
    if (!open) return;

    ImGui::Indent();

    if (estimates.empty()) {
        ImGui::TextDisabled("No estimates -- create in Cost panel");
    } else {
        for (const auto& est : estimates) {
            ImGui::PushID(static_cast<int>(est.id));
            std::string label = std::string(Icons::Cost) + " " + est.name;
            if (ImGui::Selectable(label.c_str())) {
                if (m_onCostSelected) m_onCostSelected(est.id);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Total: $%.2f | Items: %zu",
                    est.total, est.items.size());
            }
            ImGui::PopID();
        }
    }
    ImGui::Unindent();
}

void ProjectPanel::renderCutPlansSection() {
    auto project = m_projectManager->currentProject();
    if (!project || !m_cutPlanRepo) return;

    auto plans = m_cutPlanRepo->findByProject(project->id());
    std::string header = std::string(Icons::Optimizer) + " Cut Plans (" +
                         std::to_string(plans.size()) + ")";

    bool open = ImGui::CollapsingHeader(header.c_str());
    if (!open) return;

    ImGui::Indent();

    if (plans.empty()) {
        ImGui::TextDisabled("No cut plans -- save from Optimizer panel");
    } else {
        for (const auto& plan : plans) {
            ImGui::PushID(static_cast<int>(plan.id));
            std::string label = std::string(Icons::Optimizer) + " " + plan.name;
            if (ImGui::Selectable(label.c_str())) {
                if (m_onCutPlanSelected) m_onCutPlanSelected(plan.id);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Sheets: %d | Efficiency: %.1f%%",
                    plan.sheetsUsed, plan.efficiency * 100.0f);
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove from Project")) {
                    m_cutPlanRepo->remove(plan.id);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::Unindent();
}

void ProjectPanel::renderNotesSection() {
    auto project = m_projectManager->currentProject();
    if (!project) return;

    std::string header = std::string(Icons::Notes) + " Notes";
    if (ImGui::CollapsingHeader(header.c_str())) {
        ImGui::Indent();

        // Load notes into buffer on first render or project change
        if (m_notesProjectId != project->id()) {
            auto record = m_projectManager->getProjectInfo(project->id());
            if (record) {
                std::strncpy(m_notesBuf, record->notes.c_str(), sizeof(m_notesBuf) - 1);
                m_notesBuf[sizeof(m_notesBuf) - 1] = '\0';
            } else {
                m_notesBuf[0] = '\0';
            }
            m_notesProjectId = project->id();
            m_notesChanged = false;
        }

        if (ImGui::InputTextMultiline("##notes", m_notesBuf, sizeof(m_notesBuf),
                ImVec2(-1, 120))) {
            m_notesChanged = true;
        }

        if (m_notesChanged && ImGui::IsItemDeactivatedAfterEdit()) {
            // Auto-save notes via ProjectManager
            auto record = m_projectManager->getProjectInfo(project->id());
            if (record) {
                ProjectRecord updated = *record;
                updated.notes = m_notesBuf;
                // Save updated record -- ProjectRepository::update persists notes
                // We access it through the project's record
                project->record().notes = m_notesBuf;
                project->markModified();
                m_notesChanged = false;
            }
        }
        ImGui::Unindent();
    }
}

void ProjectPanel::renderNoProject() {
    ImGui::TextDisabled("No project open");
    ImGui::Spacing();

    if (ImGui::Button("New Project")) {
        if (m_projectManager) {
            auto project = m_projectManager->create("New Project");
            m_projectManager->setCurrentProject(project);
        }
    }

    // Wrap Open button to new line if narrow
    if (ImGui::GetContentRegionAvail().x > 100.0f) {
        ImGui::SameLine();
    }

    if (ImGui::Button("Open Project")) {
        if (m_openProjectCallback) {
            m_openProjectCallback();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Recent projects
    ImGui::Text("Recent Projects");
    ImGui::Spacing();

    const auto& recentProjects = Config::instance().getRecentProjects();

    if (recentProjects.empty()) {
        ImGui::TextDisabled("No recent projects.");
    } else {
        for (size_t i = 0; i < recentProjects.size(); ++i) {
            const auto& projectPath = recentProjects[i];
            ImGui::PushID(static_cast<int>(i));

            std::string name = projectPath.stem().string();
            if (name.empty()) {
                name = projectPath.filename().string();
            }

            if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_None,
                                  ImVec2(0, ImGui::GetTextLineHeightWithSpacing()))) {
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

} // namespace dw

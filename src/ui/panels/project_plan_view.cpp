#include "project_plan_view.h"

#include <algorithm>
#include <string>
#include <unordered_set>

#include <imgui.h>

#include "modules/project_plan/project_plan_presentation.h"
#include "ui/icons.h"

namespace dw {
namespace {

const char* stageIcon(project_plan::StageState state) {
    using project_plan::StageState;
    switch (state) {
    case StageState::Complete: return Icons::Check;
    case StageState::NeedsAttention: return Icons::Warning;
    case StageState::InProgress: return Icons::Play;
    case StageState::Locked: return Icons::Lock;
    case StageState::Available: return Icons::ArrowRight;
    case StageState::NotApplicable: return Icons::Info;
    }
    return Icons::Info;
}

const char* itemIcon(project_plan::ItemKind kind) {
    using project_plan::ItemKind;
    switch (kind) {
    case ItemKind::Model: return Icons::Model;
    case ItemKind::Material:
    case ItemKind::Stock:
    case ItemKind::Consumable: return Icons::Material;
    case ItemKind::Tool:
    case ItemKind::Operation: return Icons::Settings;
    case ItemKind::GCode: return Icons::GCode;
    case ItemKind::CutPlan: return Icons::Optimizer;
    case ItemKind::Cost:
    case ItemKind::Labor: return Icons::Cost;
    case ItemKind::Job: return Icons::Play;
    case ItemKind::Zeroing: return Icons::Home;
    }
    return Icons::File;
}

const project_plan::PlanNode* findNode(const project_plan::ProjectPlan& plan,
                                       workshop::ProjectItemId id) {
    const auto found = std::lower_bound(
        plan.nodes.begin(), plan.nodes.end(), id.value,
        [](const project_plan::PlanNode& node, std::int64_t value) {
            return node.item.ref.item.value < value;
        });
    return found != plan.nodes.end() && found->item.ref.item == id ? &*found : nullptr;
}

struct ResponsiveNodeLabel {
    std::string heading;
    std::string detail;
};

ResponsiveNodeLabel responsiveNodeLabel(const project_plan::PlanNode& node) {
    ResponsiveNodeLabel result{node.item.label, {}};
    const std::string fullLabel = std::string(itemIcon(node.item.kind)) + " " +
                                  node.item.label;
    const float labelBudget = std::max(
        0.0F,
        ImGui::GetContentRegionAvail().x - ImGui::GetTreeNodeToLabelSpacing());
    if (ImGui::CalcTextSize(fullLabel.c_str()).x <= labelBudget)
        return result;

    const auto separator = node.item.label.find(": ");
    if (separator != std::string::npos) {
        result.heading = node.item.label.substr(0, separator);
        result.detail = node.item.label.substr(separator + 2);
    } else {
        result.heading = project_plan::itemKindLabel(node.item.kind);
        result.detail = node.item.label;
    }
    return result;
}

void renderNode(const project_plan::ProjectPlan& plan,
                const project_plan::PlanNode& node,
                std::optional<workshop::ProjectItemId> activeItem,
                const ProjectPlanViewCallbacks& callbacks,
                std::unordered_set<std::int64_t>& rendered) {
    if (!rendered.insert(node.item.ref.item.value).second) return;
    const std::string stableId = "project_item_" +
                                 std::to_string(node.item.ref.item.value);
    ImGui::PushID(stableId.c_str());

    const bool hasChildren = !node.children.empty();
    // Keep roots visible but disclose their deeper operation/tool/run detail on
    // demand. Expanding every branch by default hid sibling designs in compact
    // Project panels and made run history overwhelm the project identity.
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (activeItem && *activeItem == node.item.ref.item)
        flags |= ImGuiTreeNodeFlags_Selected;

    const auto responsiveLabel = responsiveNodeLabel(node);
    const std::string label = std::string(itemIcon(node.item.kind)) + " " +
                              responsiveLabel.heading + "##project_item";
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
    if (!responsiveLabel.detail.empty())
        ImGui::TextWrapped("%s", responsiveLabel.detail.c_str());
    const std::string metadata =
        std::string(project_plan::itemKindLabel(node.item.kind)) + " | " +
        project_plan::itemStateLabel(node.item.state);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", metadata.c_str());
    ImGui::PopStyleColor();

    if (node.role == project_plan::NodeRole::ActivateItem) {
        const std::string button = "Open " +
                                   std::string(project_plan::itemKindLabel(node.item.kind));
        if (ImGui::SmallButton(button.c_str()) && callbacks.onActivateItem)
            callbacks.onActivateItem(node.item.ref);
    } else {
        ImGui::TextWrapped("%s", project_plan::nodeRoleLabel(node.role));
        if (!node.blockers.empty())
            ImGui::TextWrapped("%s", node.blockers.front().explanation.c_str());
    }
    ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());

    if (hasChildren && open) {
        for (const auto childId : node.children) {
            if (const auto* child = findNode(plan, childId)) {
                renderNode(plan, *child, activeItem, callbacks, rendered);
            } else {
                ImGui::TextDisabled("Information: linked project item is unavailable.");
            }
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace

void renderProjectPlanView(
    const project_plan::ProjectPlan& plan,
    std::optional<workshop::ProjectItemId> activeItem,
    const ProjectPlanViewCallbacks& callbacks) {
    const auto card = project_plan::buildContinueCardPresentation(plan);
    ImGui::SeparatorText("Continue");
    if (!card.stageLabel.empty())
        ImGui::TextDisabled("NEXT STEP: %s", card.stageLabel.c_str());
    ImGui::TextWrapped("%s", card.explanation.c_str());
    if (card.actionVisible &&
        ImGui::Button(card.actionLabel.c_str(), ImVec2(-1.0f, 0.0f)) &&
        callbacks.onNextAction) {
        callbacks.onNextAction(plan.nextAction);
    } else if (!card.actionVisible && !card.actionLabel.empty()) {
        ImGui::TextDisabled("%s", card.actionLabel.c_str());
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Project Plan");
    for (std::size_t index = 0; index < plan.stages.size(); ++index) {
        const auto& stage = plan.stages[index];
        ImGui::PushID(static_cast<int>(index));
        const std::string stageText = std::string(stageIcon(stage.state)) + " " +
                                      std::to_string(index + 1) + ". " + stage.title;
        const std::string stateText = "- " +
                                      std::string(project_plan::stageStateLabel(stage.state));
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float requiredWidth = ImGui::CalcTextSize(stageText.c_str()).x +
                                    ImGui::GetStyle().ItemSpacing.x +
                                    ImGui::CalcTextSize(stateText.c_str()).x;
        ImGui::TextUnformatted(stageText.c_str());
        if (requiredWidth <= availableWidth) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", stateText.c_str());
        } else {
            ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
            ImGui::TextDisabled("Status: %s",
                                project_plan::stageStateLabel(stage.state));
            ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
        }
        if ((stage.state == project_plan::StageState::Available ||
             stage.state == project_plan::StageState::NeedsAttention) &&
            !stage.blockers.empty()) {
            ImGui::Indent();
            ImGui::TextWrapped("%s", stage.blockers.front().explanation.c_str());
            ImGui::Unindent();
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Project Items");
    if (plan.nodes.empty()) {
        ImGui::TextDisabled("Information: no project items yet.");
        return;
    }

    std::unordered_set<std::int64_t> rendered;
    for (const auto rootId : plan.roots) {
        if (const auto* root = findNode(plan, rootId))
            renderNode(plan, *root, activeItem, callbacks, rendered);
    }
    bool unlinkedHeaderShown = false;
    for (const auto& node : plan.nodes) {
        if (rendered.find(node.item.ref.item.value) != rendered.end()) continue;
        if (!unlinkedHeaderShown) {
            ImGui::SeparatorText("Unlinked project items");
            unlinkedHeaderShown = true;
        }
        renderNode(plan, node, activeItem, callbacks, rendered);
    }
}

} // namespace dw

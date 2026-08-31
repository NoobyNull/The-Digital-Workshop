#include "project_plan_presentation.h"

#include <cstddef>

namespace dw::project_plan {

const char* stageStateLabel(StageState state) noexcept {
    switch (state) {
    case StageState::Locked: return "Locked";
    case StageState::Available: return "Next";
    case StageState::NeedsAttention: return "Needs attention";
    case StageState::InProgress: return "In progress";
    case StageState::Complete: return "Complete";
    case StageState::NotApplicable: return "Not needed";
    }
    return "Unknown";
}

const char* itemKindLabel(ItemKind kind) noexcept {
    switch (kind) {
    case ItemKind::Model: return "Design";
    case ItemKind::Material: return "Material";
    case ItemKind::Stock: return "Blank";
    case ItemKind::Tool: return "Tool";
    case ItemKind::Operation: return "Operation";
    case ItemKind::GCode: return "G-code";
    case ItemKind::CutPlan: return "Cut plan";
    case ItemKind::Cost: return "Cost";
    case ItemKind::Job: return "Run history";
    case ItemKind::Labor: return "Labor";
    case ItemKind::Consumable: return "Consumable";
    case ItemKind::Zeroing: return "Work zero";
    }
    return "Project item";
}

const char* itemStateLabel(ItemState state) noexcept {
    switch (state) {
    case ItemState::Planned: return "Planned";
    case ItemState::Ready: return "Ready";
    case ItemState::Generated: return "Generated";
    case ItemState::Sent: return "Running";
    case ItemState::Complete: return "Complete";
    case ItemState::Stale: return "Changed - refresh required";
    case ItemState::Missing: return "Missing - repair required";
    }
    return "Unknown";
}

const char* nodeRoleLabel(NodeRole role) noexcept {
    switch (role) {
    case NodeRole::ActivateItem: return "Action: Open";
    case NodeRole::RepairItem: return "Information: Needs repair";
    case NodeRole::Informational: return "Information";
    }
    return "Information";
}

bool nextActionIsActionable(NextActionKind kind) noexcept {
    return kind != NextActionKind::None;
}

ContinueCardPresentation buildContinueCardPresentation(const ProjectPlan& plan) {
    ContinueCardPresentation card;
    card.actionVisible = plan.status == BuildStatus::Ready &&
                         nextActionIsActionable(plan.nextAction.kind);
    const auto stage = static_cast<std::size_t>(plan.nextAction.stage);
    if (card.actionVisible) {
        if (plan.nextAction.kind == NextActionKind::MonitorRun)
            card.stageLabel = "Run CNC";
        else if (stage < plan.stages.size())
            card.stageLabel = plan.stages[stage].title;
    }
    card.actionLabel = plan.nextAction.label;
    card.explanation = plan.nextAction.explanation;
    return card;
}

} // namespace dw::project_plan

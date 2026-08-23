// Reconcile deterministic Direct Carve tool-role children with the active plan.

#include "ui/panels/direct_carve_tool_child_sync.h"

#include <utility>

#include <nlohmann/json.hpp>

#include "core/project/project.h"
#include "ui/panels/direct_carve_panel.h"

namespace dw {
namespace {

const char* toolTypeLabel(VtdbToolType type) {
    switch (type) {
    case VtdbToolType::BallNose:
        return "ball_nose";
    case VtdbToolType::EndMill:
        return "end_mill";
    case VtdbToolType::Radiused:
        return "radiused";
    case VtdbToolType::VBit:
        return "v_bit";
    case VtdbToolType::TaperedBallNose:
        return "tapered_ball_nose";
    case VtdbToolType::Drill:
        return "drill";
    case VtdbToolType::ThreadMill:
        return "thread_mill";
    case VtdbToolType::FormTool:
        return "form_tool";
    case VtdbToolType::DiamondDrag:
        return "diamond_drag";
    }
    return "unknown";
}

bool isToolRole(const std::string& role) {
    return role == "finish" || role == "clear";
}

std::string operationSourceKey(const ProjectOpenItem& operation) {
    return operation.sourceKey.empty() ? "project_item:" + std::to_string(operation.id)
                                       : operation.sourceKey;
}

} // namespace

nlohmann::json directCarveToolSummaryJson(const VtdbToolGeometry& tool) {
    return {
        {"id", tool.id},
        {"name", resolveToolNameFormat(tool)},
        {"type", toolTypeLabel(tool.tool_type)},
        {"units", tool.units == VtdbUnits::Imperial ? "imperial" : "metric"},
        {"diameter_mm", tool.units == VtdbUnits::Imperial ? tool.diameter * 25.4 : tool.diameter},
        {"included_angle_deg", tool.included_angle},
        {"flat_diameter", tool.flat_diameter},
        {"tip_radius", tool.tip_radius},
        {"flutes", tool.num_flutes},
    };
}

bool DirectCarvePanel::reconcileToolOpenItems() {
    const auto& finishing = m_toolPlan.finishingIntent();
    bool reconciled = finishing ? syncToolOpenItem("finish", *finishing).has_value()
                                : removeToolOpenItems("finish");

    const auto& effectiveClearing = m_toolPlan.effectiveClearingTool();
    const auto& selectedClearing = m_toolPlan.clearingIntent();
    const VtdbToolGeometry* plannedClearing = nullptr;
    if (finishing) {
        switch (m_toolPlan.clearingMode()) {
        case carve::ClearingToolMode::Automatic:
            plannedClearing = effectiveClearing ? &*effectiveClearing : nullptr;
            break;
        case carve::ClearingToolMode::Selected:
            plannedClearing = effectiveClearing ? &*effectiveClearing
                                                : (selectedClearing ? &*selectedClearing : nullptr);
            break;
        case carve::ClearingToolMode::Disabled:
            break;
        }
    }

    reconciled = (plannedClearing ? syncToolOpenItem("clear", *plannedClearing).has_value()
                                  : removeToolOpenItems("clear")) &&
                 reconciled;
    return reconciled;
}

std::optional<i64> DirectCarvePanel::syncToolOpenItem(const std::string& role,
                                                      const VtdbToolGeometry& tool) {
    const auto operation = pinnedOperationOpenItem();
    if (!operation || !m_preparationPin || !isToolRole(role)) {
        return std::nullopt;
    }

    const std::string parentSourceKey = operationSourceKey(*operation);
    nlohmann::json intent = {
        {"role", role},
        {"operation_source_key", parentSourceKey},
        {"required_for", role == "clear" ? "clearing_pass" : "finishing_pass"},
    };

    ProjectOpenItem item;
    item.projectId = m_preparationPin->project().value;
    item.itemType = ProjectOpenItemType::Tool;
    item.sourceTable = "direct_carve";
    item.sourceKey = parentSourceKey + ":tool:" + role;
    item.parentItemId = m_preparationPin->operationItem().item.value;
    item.status = ProjectOpenItemStatus::Ready;
    item.displayName = (role == "clear" ? "Clearing Tool: " : "Finishing Tool: ") +
                       resolveToolNameFormat(tool);
    item.intentJson = intent.dump();
    item.snapshotJson = directCarveToolSummaryJson(tool).dump();
    return m_projectManager->upsertOpenItem(std::move(item));
}

bool DirectCarvePanel::removeToolOpenItems(const std::string& role) {
    const auto operation = pinnedOperationOpenItem();
    if (!operation || !m_projectManager || !isToolRole(role)) {
        return false;
    }

    const std::string roleSourceKey = operationSourceKey(*operation) + ":tool:" + role;
    bool removedAll = true;
    for (const auto& item : m_projectManager->currentOpenItems()) {
        const bool ownedRoleChild = item.projectId == operation->projectId &&
                                    item.itemType == ProjectOpenItemType::Tool &&
                                    item.sourceTable == "direct_carve" &&
                                    item.sourceKey == roleSourceKey && item.parentItemId &&
                                    *item.parentItemId == operation->id;
        if (ownedRoleChild) {
            removedAll = m_projectManager->removeOpenItem(item.id) && removedAll;
        }
    }
    return removedAll;
}

} // namespace dw

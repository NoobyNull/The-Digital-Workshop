#include "project_open_item_warnings.h"

namespace dw {

namespace {

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
    case ProjectOpenItemType::Zeroing: return "Zeroing";
    }
    return "Item";
}

const char* warningStatusLabel(ProjectOpenItemStatus status) {
    switch (status) {
    case ProjectOpenItemStatus::Stale: return "stale";
    case ProjectOpenItemStatus::Missing: return "missing";
    default: break;
    }
    return "";
}

} // namespace

std::vector<std::string>
buildProjectOpenItemWarningLines(const std::vector<ProjectOpenItem>& items) {
    std::vector<std::string> warnings;
    for (const auto& item : items) {
        if (item.status != ProjectOpenItemStatus::Stale &&
            item.status != ProjectOpenItemStatus::Missing) {
            continue;
        }
        warnings.push_back(std::string(labelForOpenItemType(item.itemType)) + " '" +
                           item.displayName + "' is " + warningStatusLabel(item.status));
    }
    return warnings;
}

} // namespace dw

#include "gcode_project_context.h"

#include <algorithm>

namespace dw {

namespace {

const ProjectOpenItem* findById(const std::vector<ProjectOpenItem>& items, i64 id) {
    auto it = std::find_if(items.begin(), items.end(), [id](const ProjectOpenItem& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &*it;
}

const char* statusLabel(ProjectOpenItemStatus status) {
    switch (status) {
    case ProjectOpenItemStatus::Planned: return "planned";
    case ProjectOpenItemStatus::Ready: return "ready";
    case ProjectOpenItemStatus::Generated: return "generated";
    case ProjectOpenItemStatus::Sent: return "sent";
    case ProjectOpenItemStatus::Complete: return "complete";
    case ProjectOpenItemStatus::Stale: return "stale";
    case ProjectOpenItemStatus::Missing: return "missing";
    }
    return "unknown";
}

void appendWarningIfNeeded(std::vector<std::string>& lines,
                           std::string_view label,
                           const ProjectOpenItem& item) {
    if (item.status != ProjectOpenItemStatus::Stale &&
        item.status != ProjectOpenItemStatus::Missing) {
        return;
    }

    lines.push_back("WARNING: " + std::string(label) + " '" + item.displayName +
                    "' is " + statusLabel(item.status));
}

bool isRequiredToolForGCode(const ProjectOpenItem& tool,
                            const ProjectOpenItem& gcode,
                            i64 gcodeId) {
    if (tool.itemType != ProjectOpenItemType::Tool) {
        return false;
    }
    if (tool.parentItemId.has_value() && gcode.parentItemId.has_value() &&
        *tool.parentItemId == *gcode.parentItemId) {
        return true;
    }
    auto prefix = "gcode_files:" + std::to_string(gcodeId) + ":tool:";
    return tool.sourceKey.rfind(prefix, 0) == 0;
}

} // namespace

std::vector<std::string>
buildGCodeProjectContextLines(std::string_view projectName,
                              const std::vector<ProjectOpenItem>& openItems,
                              i64 gcodeId) {
    std::vector<std::string> lines;
    if (!projectName.empty()) {
        lines.push_back("Project: " + std::string(projectName));
    }

    const ProjectOpenItem* gcode = nullptr;
    for (const auto& item : openItems) {
        if (item.itemType == ProjectOpenItemType::Gcode &&
            item.sourceTable == "gcode_files" &&
            item.sourceId.has_value() &&
            *item.sourceId == gcodeId) {
            gcode = &item;
            break;
        }
    }

    if (!gcode) {
        lines.push_back("G-code: not linked to current project");
        return lines;
    }

    lines.push_back("G-code: " + gcode->displayName);

    const ProjectOpenItem* operation = nullptr;
    if (gcode->parentItemId.has_value()) {
        operation = findById(openItems, *gcode->parentItemId);
    }
    if (operation && operation->itemType == ProjectOpenItemType::Operation) {
        lines.push_back("Operation: " + operation->displayName);
    }

    for (const auto& item : openItems) {
        if (isRequiredToolForGCode(item, *gcode, gcodeId)) {
            lines.push_back("Required tool: " + item.displayName);
        }
    }

    appendWarningIfNeeded(lines, "G-code", *gcode);
    if (operation && operation->itemType == ProjectOpenItemType::Operation) {
        appendWarningIfNeeded(lines, "Operation", *operation);
    }
    for (const auto& item : openItems) {
        if (isRequiredToolForGCode(item, *gcode, gcodeId)) {
            appendWarningIfNeeded(lines, "Tool", item);
        }
    }

    return lines;
}

} // namespace dw

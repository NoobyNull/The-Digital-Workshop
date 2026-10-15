#include "app/project_contents_adapter.h"

#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace dw {
namespace {

ProjectContentKind contentKind(ProjectOpenItemType type) {
    switch (type) {
    case ProjectOpenItemType::Model: return ProjectContentKind::Design;
    case ProjectOpenItemType::Operation: return ProjectContentKind::Setup;
    case ProjectOpenItemType::Material: return ProjectContentKind::Material;
    case ProjectOpenItemType::Tool: return ProjectContentKind::Tool;
    case ProjectOpenItemType::Gcode: return ProjectContentKind::GCode;
    case ProjectOpenItemType::Job: return ProjectContentKind::Run;
    }
    return ProjectContentKind::Design;
}

const char* statusLabel(ProjectContentKind kind, ProjectOpenItemStatus status) {
    switch (status) {
    case ProjectOpenItemStatus::Planned: return "Planned";
    case ProjectOpenItemStatus::Ready: return "Ready";
    case ProjectOpenItemStatus::Generated: return "Generated";
    case ProjectOpenItemStatus::Sent: return "Sent to machine";
    case ProjectOpenItemStatus::Complete: return "Complete";
    case ProjectOpenItemStatus::Stale:
        return kind == ProjectContentKind::Run ? "Interrupted" : "Out of date";
    case ProjectOpenItemStatus::Missing: return "Missing";
    }
    return "Unknown";
}

bool needsAttention(ProjectOpenItemStatus status) {
    return status == ProjectOpenItemStatus::Stale || status == ProjectOpenItemStatus::Missing;
}

} // namespace

ProjectContents makeProjectContents(workshop::ProjectId project,
                                    const std::vector<ProjectOpenItem>& items) {
    std::vector<const ProjectOpenItem*> shown;
    std::unordered_set<i64> shownIds;
    for (const auto& item : items) {
        if (item.projectId != project.value) continue;
        if (shownIds.insert(item.id).second) shown.push_back(&item);
    }

    std::unordered_map<i64, std::vector<const ProjectOpenItem*>> children;
    std::vector<const ProjectOpenItem*> roots;
    for (const auto* item : shown) {
        if (item->parentItemId && *item->parentItemId != item->id &&
            shownIds.count(*item->parentItemId) != 0) {
            children[*item->parentItemId].push_back(item);
        } else {
            roots.push_back(item);
        }
    }

    ProjectContents rows;
    rows.reserve(shown.size());
    std::unordered_set<i64> emitted;
    const auto emit = [&](const ProjectOpenItem& item, bool asRoot) {
        const auto kind = contentKind(item.itemType);
        ProjectContentRow row;
        row.ref = {project, workshop::ProjectItemId(item.id)};
        if (!asRoot && item.parentItemId)
            row.parent = workshop::ProjectItemId(*item.parentItemId);
        row.kind = kind;
        row.label = item.displayName;
        row.status = statusLabel(kind, item.status);
        row.needsAttention = needsAttention(item.status);
        rows.push_back(std::move(row));
        emitted.insert(item.id);
    };

    // Depth-first from each root keeps every child directly after its parent.
    std::vector<const ProjectOpenItem*> stack;
    for (const auto* root : roots) {
        emit(*root, true);
        const auto rootChildren = children.find(root->id);
        if (rootChildren == children.end()) continue;
        stack.assign(rootChildren->second.rbegin(), rootChildren->second.rend());
        while (!stack.empty()) {
            const auto* item = stack.back();
            stack.pop_back();
            if (emitted.count(item->id) != 0) continue;
            emit(*item, false);
            const auto nested = children.find(item->id);
            if (nested == children.end()) continue;
            stack.insert(stack.end(), nested->second.rbegin(), nested->second.rend());
        }
    }

    // Items caught in a parent cycle are unreachable from any root; keep them
    // visible at the root rather than dropping them.
    for (const auto* item : shown) {
        if (emitted.count(item->id) == 0) emit(*item, true);
    }
    return rows;
}

} // namespace dw

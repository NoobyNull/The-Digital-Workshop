#include "project_plan_input_adapter.h"

#include <nlohmann/json.hpp>

namespace dw {
namespace {

using project_plan::Evidence;

nlohmann::json objectFrom(const std::string& text) {
    auto json = nlohmann::json::parse(text, nullptr, false);
    return json.is_object() ? std::move(json) : nlohmann::json::object();
}

Evidence boolEvidence(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || !object[key].is_boolean()) return Evidence::Unknown;
    return object[key].get<bool>() ? Evidence::Satisfied : Evidence::Unsatisfied;
}

bool positiveNumber(const nlohmann::json& object, const char* key) {
    return object.contains(key) && object[key].is_number() &&
           object[key].get<double>() > 0.0;
}

bool number(const nlohmann::json& object, const char* key) {
    return object.contains(key) && object[key].is_number();
}

project_plan::OperationFacts operationFacts(
    const ProjectOpenItem& operation,
    const std::vector<ProjectOpenItem>& items) {
    project_plan::OperationFacts facts;
    facts.operation = {workshop::ProjectId(operation.projectId),
                       workshop::ProjectItemId(operation.id)};

    const auto intent = objectFrom(operation.intentJson);
    if (intent.value("operation_kind", std::string()) != "direct_carve") return facts;

    const auto snapshot = objectFrom(operation.snapshotJson);
    const auto setup = snapshot.value("setup", nlohmann::json::object());
    facts.modelLoaded = boolEvidence(setup, "model_loaded");
    facts.materialSelected = boolEvidence(setup, "material_selected");
    facts.finishingToolSelected = boolEvidence(setup, "finishing_tool_selected");
    facts.toolpathGenerated = boolEvidence(setup, "toolpath_generated");

    const auto stock = intent.value("stock", nlohmann::json::object());
    if (stock.is_object() && number(stock, "width_mm") &&
        number(stock, "height_mm") && number(stock, "thickness_mm")) {
        facts.blankSpecified = positiveNumber(stock, "width_mm") &&
                                       positiveNumber(stock, "height_mm") &&
                                       positiveNumber(stock, "thickness_mm")
                                   ? Evidence::Satisfied
                                   : Evidence::Unsatisfied;
    }

    if (facts.toolpathGenerated == Evidence::Unsatisfied) {
        facts.toolpathFresh = Evidence::Unsatisfied;
    } else if (facts.toolpathGenerated == Evidence::Satisfied &&
               setup.contains("settings_version") &&
               setup["settings_version"].is_number_integer() &&
               setup.contains("generated_at_version") &&
               setup["generated_at_version"].is_number_integer()) {
        facts.toolpathFresh =
            setup["settings_version"].get<int>() ==
                    setup["generated_at_version"].get<int>()
                ? Evidence::Satisfied
                : Evidence::Unsatisfied;
    }

    for (const auto& item : items) {
        if (item.itemType != ProjectOpenItemType::Zeroing ||
            item.projectId != operation.projectId ||
            item.parentItemId != operation.id) {
            continue;
        }
        const auto zero = objectFrom(item.snapshotJson);
        const auto evidence = boolEvidence(zero, "zero_verified");
        if (evidence != Evidence::Unknown) facts.zeroVerified = evidence;
    }
    return facts;
}

} // namespace

project_plan::ItemKind toProjectPlanItemKind(ProjectOpenItemType type) noexcept {
    switch (type) {
    case ProjectOpenItemType::Model: return project_plan::ItemKind::Model;
    case ProjectOpenItemType::Material: return project_plan::ItemKind::Material;
    case ProjectOpenItemType::Stock: return project_plan::ItemKind::Stock;
    case ProjectOpenItemType::Tool: return project_plan::ItemKind::Tool;
    case ProjectOpenItemType::Operation: return project_plan::ItemKind::Operation;
    case ProjectOpenItemType::Gcode: return project_plan::ItemKind::GCode;
    case ProjectOpenItemType::CutPlan: return project_plan::ItemKind::CutPlan;
    case ProjectOpenItemType::Cost: return project_plan::ItemKind::Cost;
    case ProjectOpenItemType::Job: return project_plan::ItemKind::Job;
    case ProjectOpenItemType::Labor: return project_plan::ItemKind::Labor;
    case ProjectOpenItemType::Consumable: return project_plan::ItemKind::Consumable;
    case ProjectOpenItemType::Zeroing: return project_plan::ItemKind::Zeroing;
    }
    return project_plan::ItemKind::Model;
}

project_plan::ItemState toProjectPlanItemState(ProjectOpenItemStatus status) noexcept {
    switch (status) {
    case ProjectOpenItemStatus::Planned: return project_plan::ItemState::Planned;
    case ProjectOpenItemStatus::Ready: return project_plan::ItemState::Ready;
    case ProjectOpenItemStatus::Generated: return project_plan::ItemState::Generated;
    case ProjectOpenItemStatus::Sent: return project_plan::ItemState::Sent;
    case ProjectOpenItemStatus::Complete: return project_plan::ItemState::Complete;
    case ProjectOpenItemStatus::Stale: return project_plan::ItemState::Stale;
    case ProjectOpenItemStatus::Missing: return project_plan::ItemState::Missing;
    }
    return project_plan::ItemState::Planned;
}

project_plan::ProjectPlanInput makeProjectPlanInput(
    workshop::ProjectId project,
    std::string_view projectName,
    const std::vector<ProjectOpenItem>& items,
    std::optional<workshop::ProjectItemRef> focusedItem) {
    project_plan::ProjectPlanInput input;
    input.project = project;
    input.projectName = std::string(projectName);
    bool focusedItemExists = false;
    for (const auto& item : items) {
        if (item.projectId != project.value) continue;
        focusedItemExists = focusedItemExists ||
                            (focusedItem && focusedItem->project == project &&
                             focusedItem->item.value == item.id);
        project_plan::ItemSnapshot snapshot;
        snapshot.ref = {workshop::ProjectId(item.projectId),
                        workshop::ProjectItemId(item.id)};
        snapshot.kind = toProjectPlanItemKind(item.itemType);
        snapshot.state = toProjectPlanItemState(item.status);
        snapshot.label = item.displayName;
        if (item.parentItemId)
            snapshot.parent = workshop::ProjectItemId(*item.parentItemId);
        input.items.push_back(std::move(snapshot));
        if (item.itemType == ProjectOpenItemType::Operation) {
            input.operations.push_back(operationFacts(item, items));
        }
    }
    if (focusedItemExists) input.focusedItem = focusedItem;
    return input;
}

} // namespace dw

#include "library_panel.h"

#include <algorithm>
#include <optional>
#include <string>

#include <imgui.h>

#include "../../core/utils/log.h"
#include "../ui_colors.h"
#include "../widgets/toast.h"

namespace dw {

void LibraryPanel::renderCategoryAssignDialog() {
    if (m_showCategoryAssignDialog) {
        ImGui::OpenPopup("Assign Category");
        m_showCategoryAssignDialog = false;
        m_assignedCategoryIds.clear();
        if (m_library && m_selectedModelIds.size() == 1) {
            const auto modelId = *m_selectedModelIds.begin();
            for (const auto& category : m_categories) {
                const auto models = m_library->filterByCategory(category.id);
                if (std::any_of(models.begin(), models.end(), [modelId](const auto& model) {
                        return model.id == modelId;
                    })) {
                    m_assignedCategoryIds.insert(category.id);
                }
            }
        }
        m_newCategoryName[0] = '\0';
        m_newCategoryParent = -1;
    }

    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSize(
        ImVec2(viewport->WorkSize.x * 0.25F, viewport->WorkSize.y * 0.4F),
        ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Assign Category", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Select categories for model:");
    ImGui::Separator();
    ImGui::BeginChild("CatList", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.6F), true);
    for (const auto& category : m_categories) {
        if (category.parentId.has_value())
            continue;
        bool checked = m_assignedCategoryIds.count(category.id) > 0;
        if (ImGui::Checkbox(category.name.c_str(), &checked)) {
            if (checked)
                m_assignedCategoryIds.insert(category.id);
            else
                m_assignedCategoryIds.erase(category.id);
        }

        ImGui::Indent(ImGui::GetStyle().IndentSpacing);
        for (const auto& child : m_categories) {
            if (!child.parentId.has_value() || *child.parentId != category.id)
                continue;
            bool childChecked = m_assignedCategoryIds.count(child.id) > 0;
            if (ImGui::Checkbox(child.name.c_str(), &childChecked)) {
                if (childChecked)
                    m_assignedCategoryIds.insert(child.id);
                else
                    m_assignedCategoryIds.erase(child.id);
            }
        }
        ImGui::Unindent(ImGui::GetStyle().IndentSpacing);
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6F);
    ImGui::InputTextWithHint(
        "##NewCat", "New category name...", m_newCategoryName, sizeof(m_newCategoryName));
    ImGui::SameLine();
    if (ImGui::Button("Add") && m_newCategoryName[0] != '\0') {
        std::optional<int64_t> parentId;
        if (m_newCategoryParent > 0)
            parentId = m_newCategoryParent;
        const auto newId = m_library->createCategory(m_newCategoryName, parentId);
        if (newId) {
            m_categories = m_library->getAllCategories();
            m_assignedCategoryIds.insert(*newId);
        }
        m_newCategoryName[0] = '\0';
    }

    ImGui::Separator();
    const float buttonWidth =
        ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 4.0F;
    if (ImGui::Button("Apply", ImVec2(buttonWidth, 0))) {
        if (m_library) {
            for (const auto modelId : m_selectedModelIds) {
                for (const auto& category : m_categories) {
                    const auto models = m_library->filterByCategory(category.id);
                    const bool wasAssigned =
                        std::any_of(models.begin(), models.end(), [modelId](const auto& model) {
                            return model.id == modelId;
                        });
                    const bool assigned = m_assignedCategoryIds.count(category.id) > 0;
                    if (wasAssigned && !assigned)
                        m_library->removeModelCategory(modelId, category.id);
                    else if (!wasAssigned && assigned)
                        m_library->assignCategory(modelId, category.id);
                }
            }
            refresh();
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void LibraryPanel::renderDeleteConfirm() {
    if (m_showDeleteConfirm) {
        ImGui::OpenPopup("Delete Library Item?");
        m_showDeleteConfirm = false;
    }

    ImGui::SetNextWindowPos(
        ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    if (!ImGui::BeginPopupModal("Delete Library Item?", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::Text("Remove \"%s\" from the Library?", m_deleteItemName.c_str());
    ImGui::TextWrapped(
        "Digital Workshop will first check whether any project still uses this source.");
    ImGui::TextDisabled("Project-linked sources are kept safe.");
    if (!m_deleteResultMessage.empty()) {
        const auto color = m_deleteResultStatus == design_library::LibraryDeleteResultStatus::Blocked
                               ? colors::kWarning
                               : colors::kErrorText;
        ImGui::TextColored(color, "%s", m_deleteResultMessage.c_str());
    }
    ImGui::Spacing();

    const float buttonWidth =
        ImGui::CalcTextSize("Keep item").x + ImGui::GetStyle().FramePadding.x * 4.0F;
    if (ImGui::Button("Delete", ImVec2(buttonWidth, 0))) {
        const auto rawResult = emitLibraryIntent(
            design_library::LibraryDeleteRequested{m_deleteItems});
        const auto* result = std::get_if<design_library::LibraryDeleteResult>(&rawResult);
        if (!result) {
            m_deleteResultStatus = design_library::LibraryDeleteResultStatus::Failed;
            m_deleteResultMessage = "Deletion failed because the application returned an "
                                    "invalid result.";
        } else if ((result->status == design_library::LibraryDeleteResultStatus::Deleted ||
                    result->status ==
                        design_library::LibraryDeleteResultStatus::PartiallyDeleted) &&
                   design_library::isConfirmedLibraryDeletion(m_deleteItems, *result)) {
            const auto count = result->confirmedItems.size();
            applyConfirmedDeletion(result->confirmedItems);
            ToastManager::instance().show(
                result->status == design_library::LibraryDeleteResultStatus::Deleted
                    ? ToastType::Success
                    : ToastType::Warning,
                result->status == design_library::LibraryDeleteResultStatus::Deleted
                    ? "Deleted from Library"
                    : "Some Items Were Deleted",
                result->message.empty()
                    ? std::to_string(count) + (count == 1 ? " item deleted" : " items deleted")
                    : result->message);
            m_deleteItems.clear();
            m_deleteResultMessage.clear();
            ImGui::CloseCurrentPopup();
        } else {
            m_deleteResultStatus = (result->status ==
                                        design_library::LibraryDeleteResultStatus::Deleted ||
                                    result->status == design_library::
                                                          LibraryDeleteResultStatus::PartiallyDeleted)
                                       ? design_library::LibraryDeleteResultStatus::Failed
                                       : result->status;
            m_deleteResultMessage =
                result->message.empty()
                    ? (m_deleteResultStatus == design_library::LibraryDeleteResultStatus::Blocked
                           ? "This source is still used by a project and was not deleted."
                           : "The Library item could not be deleted. Nothing was removed.")
                    : result->message;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Keep item", ImVec2(buttonWidth, 0))) {
        m_deleteItems.clear();
        m_deleteResultMessage.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void LibraryPanel::renderRenameDialog() {
    if (m_showRenameDialog) {
        ImGui::OpenPopup("Rename Model");
        m_showRenameDialog = false;
    }

    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.25F, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Rename Model", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const float buttonWidth =
        ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 4.0F;
    ImGui::TextUnformatted("Enter new name:");
    ImGui::SetNextItemWidth(-1);
    const bool enterPressed = ImGui::InputText("##RenameInput",
                                               m_renameBuffer,
                                               sizeof(m_renameBuffer),
                                               ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere(-1);
    ImGui::Spacing();

    const bool savePressed = ImGui::Button("Save", ImVec2(buttonWidth, 0));
    ImGui::SameLine();
    const bool cancelPressed = ImGui::Button("Cancel", ImVec2(buttonWidth, 0));
    if (savePressed || enterPressed) {
        const auto newName = design_library::trimLibraryProjectName(m_renameBuffer);
        if (newName.empty()) {
            ToastManager::instance().show(
                ToastType::Warning, "Invalid Name", "Name cannot be empty");
        } else if (m_library) {
            auto record = m_library->getModel(m_renameModelId);
            if (record) {
                record->name = newName;
                if (m_library->updateModel(*record)) {
                    refresh();
                    ToastManager::instance().show(
                        ToastType::Success, "Renamed", "Model renamed successfully");
                    ImGui::CloseCurrentPopup();
                } else {
                    ToastManager::instance().show(
                        ToastType::Error, "Rename Failed", "Could not rename model");
                    log::error("Library", "Failed to rename model");
                }
            }
        }
    }
    if (cancelPressed)
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace dw

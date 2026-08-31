#include "library_picker_presentation.h"

#include <algorithm>
#include <utility>

namespace dw::design_library {
namespace {

bool sameItem(workshop::LibraryItemRef lhs, workshop::LibraryItemRef rhs) noexcept {
    return lhs.kind == rhs.kind && lhs.item == rhs.item;
}

std::size_t memberCount(const LibraryPickerSnapshot& snapshot) noexcept {
    return static_cast<std::size_t>(
        std::count_if(snapshot.selectedItems.begin(),
                      snapshot.selectedItems.end(),
                      [&snapshot](const auto item) { return snapshot.isProjectMember(item); }));
}

bool sameOptionalItem(const std::optional<workshop::LibraryItemRef>& item,
                      workshop::LibraryItemRef candidate) noexcept {
    return item.has_value() && sameItem(*item, candidate);
}

std::string itemCountText(std::size_t count) {
    return std::to_string(count) + (count == 1 ? " item" : " items");
}

std::string projectLabel(const LibraryPickerSnapshot& snapshot) {
    return snapshot.activeProjectName.empty() ? "your project" : snapshot.activeProjectName;
}

bool hasProjectModel(const LibraryPickerSnapshot& snapshot) noexcept {
    return std::any_of(snapshot.projectMembership.begin(),
                       snapshot.projectMembership.end(),
                       [](const auto item) {
                           return item.kind == workshop::LibraryItemKind::Model;
                       });
}

bool isSingleSelectedModel(const LibraryPickerSnapshot& snapshot) noexcept {
    return snapshot.selectedItems.size() == 1 &&
           snapshot.selectedItems.front().kind == workshop::LibraryItemKind::Model;
}

} // namespace

std::string trimLibraryProjectName(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

LibraryPickerActionLayout chooseLibraryPickerActionLayout(
    float availableWidth,
    const std::vector<float>& labelWidths,
    float horizontalFramePadding,
    float itemSpacing) {
    float requiredWidth = 0.0F;
    for (const auto labelWidth : labelWidths)
        requiredWidth += std::max(0.0F, labelWidth) + horizontalFramePadding * 2.0F;
    if (labelWidths.size() > 1)
        requiredWidth += itemSpacing * static_cast<float>(labelWidths.size() - 1);
    return availableWidth >= requiredWidth ? LibraryPickerActionLayout::Inline
                                           : LibraryPickerActionLayout::Stacked;
}

bool isConfirmedLibraryDeletion(
    const std::vector<workshop::LibraryItemRef>& requested,
    const LibraryDeleteResult& result) noexcept {
    if ((result.status != LibraryDeleteResultStatus::Deleted &&
         result.status != LibraryDeleteResultStatus::PartiallyDeleted) ||
        result.confirmedItems.empty())
        return false;
    for (std::size_t index = 0; index < result.confirmedItems.size(); ++index) {
        const auto item = result.confirmedItems[index];
        if (!item.valid() ||
            std::none_of(requested.begin(), requested.end(), [item](const auto candidate) {
                return sameItem(item, candidate);
            })) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (sameItem(item, result.confirmedItems[prior]))
                return false;
        }
    }
    return true;
}

LibraryPickerPresentation makeLibraryPickerPresentation(const LibraryPickerSnapshot& snapshot,
                                                         std::string selectedItemName,
                                                         std::string errorMessage) {
    LibraryPickerPresentation result;
    result.visible = snapshot.active;
    result.purpose = snapshot.purpose;
    result.errorText = std::move(errorMessage);
    result.selectedCount = snapshot.selectedItems.size();
    result.alreadyMemberCount = memberCount(snapshot);
    result.actionItemCount = result.selectedCount - result.alreadyMemberCount;
    result.previewPending = snapshot.pendingPreviewToken.has_value();
    result.actionPending = snapshot.pendingActionToken.has_value() || snapshot.returnPending;
    result.suggestedProjectName = trimLibraryProjectName(std::move(selectedItemName));

    if (!result.visible)
        return result;

    switch (snapshot.purpose) {
    case LibraryPickerPurpose::ManageLibrary:
        result.heading = "Design Library";
        result.guidance = "Choose an item, then use Preview when you want to inspect it.";
        result.cancelLabel = "Back";
        break;
    case LibraryPickerPurpose::StartProject:
        result.heading = "Choose a model for your new project";
        result.guidance = "Select one model. Preview it first, or start a named project when ready.";
        result.primaryVisible = true;
        result.primaryLabel = "Start project with this model";
        result.cancelLabel = "Cancel";
        break;
    case LibraryPickerPurpose::AddToProject:
        result.heading = "Choose a model for " + projectLabel(snapshot);
        result.guidance =
            "Select one model. Preview it first, or choose it for this project when ready.";
        result.primaryVisible = true;
        result.cancelLabel = "Back to project";
        result.primaryLabel =
            isSingleSelectedModel(snapshot) && result.actionItemCount == 0
                ? "Current model"
                : "Choose this model";
        break;
    }

    if (result.selectedCount == 0) {
        result.selectionText = "Nothing selected";
    } else if (result.selectedCount == 1 && !result.suggestedProjectName.empty()) {
        result.selectionText = "Selected: " + result.suggestedProjectName;
    } else {
        result.selectionText = itemCountText(result.selectedCount) + " selected";
    }
    if (snapshot.purpose == LibraryPickerPurpose::AddToProject && result.selectedCount > 0) {
        if (!isSingleSelectedModel(snapshot)) {
            result.membershipText = "Choose exactly one model for this project.";
        } else if (snapshot.isProjectMember(snapshot.selectedItems.front())) {
            result.membershipText =
                "This is the model already chosen for " + projectLabel(snapshot) + ".";
        } else if (hasProjectModel(snapshot)) {
            result.membershipText = projectLabel(snapshot) +
                                    " already has a model. Start a new project to use this model.";
        } else {
            result.membershipText =
                "Preview does not choose the model. Use \"Choose this model\" when ready.";
        }
    }

    const bool operationPending = result.previewPending || result.actionPending;
    result.previewEnabled =
        (snapshot.purpose == LibraryPickerPurpose::ManageLibrary
             ? result.selectedCount > 0
             : isSingleSelectedModel(snapshot)) &&
        !operationPending;
    result.primaryEnabled = snapshot.canConfirm() && !result.previewPending;
    result.cancelEnabled = !snapshot.pendingActionToken.has_value() && !snapshot.returnPending;
    result.previewLabel = result.previewPending ? "Loading preview..." : "Preview selected";

    if (snapshot.returnPending) {
        result.statusText = "Returning to your previous work...";
    } else if (snapshot.startRequestPending) {
        result.statusText = "Starting your project...";
    } else if (snapshot.pendingActionToken.has_value()) {
        result.statusText = "Saving model...";
    } else if (snapshot.pendingPreviewToken.has_value()) {
        result.statusText = "Loading preview...";
    } else if (result.selectedCount == 1 && snapshot.previewItem.has_value() &&
               sameOptionalItem(snapshot.previewItem, snapshot.selectedItems.front())) {
        result.statusText = "Preview ready";
    }

    return result;
}

} // namespace dw::design_library

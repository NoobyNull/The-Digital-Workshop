#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "library_picker_flow.h"

namespace dw::design_library {

struct LibrarySelectionChanged {
    std::vector<workshop::LibraryItemRef> items;
};

struct LibraryPreviewRequested {
    workshop::LibraryItemRef item;
};

struct LibraryPrimaryActionRequested {
    LibraryPickerPurpose purpose = LibraryPickerPurpose::ManageLibrary;
    std::string projectName;
};

struct LibraryCancelRequested {};

struct LibraryDeleteRequested {
    std::vector<workshop::LibraryItemRef> items;
};

using LibraryPanelIntent = std::variant<LibrarySelectionChanged,
                                        LibraryPreviewRequested,
                                        LibraryPrimaryActionRequested,
                                        LibraryCancelRequested,
                                        LibraryDeleteRequested>;

enum class LibraryActionResultStatus {
    Accepted,
    Pending,
    Rejected,
};

struct LibraryActionResult {
    LibraryActionResultStatus status = LibraryActionResultStatus::Accepted;
    std::string message;

    [[nodiscard]] bool accepted() const noexcept {
        return status != LibraryActionResultStatus::Rejected;
    }
};

enum class LibraryDeleteResultStatus {
    Deleted,
    PartiallyDeleted,
    Blocked,
    Failed,
};

struct LibraryDeleteResult {
    LibraryDeleteResultStatus status = LibraryDeleteResultStatus::Failed;
    std::vector<workshop::LibraryItemRef> confirmedItems;
    std::string message;
};

using LibraryPanelIntentResult = std::variant<LibraryActionResult, LibraryDeleteResult>;

struct LibraryPickerPresentation {
    bool visible = false;
    LibraryPickerPurpose purpose = LibraryPickerPurpose::ManageLibrary;
    std::string heading;
    std::string guidance;
    std::string selectionText;
    std::string membershipText;
    std::string previewLabel;
    std::string primaryLabel;
    std::string cancelLabel;
    std::string statusText;
    std::string errorText;
    std::string suggestedProjectName;
    std::size_t selectedCount = 0;
    std::size_t alreadyMemberCount = 0;
    std::size_t actionItemCount = 0;
    bool previewEnabled = false;
    bool primaryVisible = false;
    bool primaryEnabled = false;
    bool cancelEnabled = false;
    bool previewPending = false;
    bool actionPending = false;
};

enum class LibraryPickerActionLayout {
    Inline,
    Stacked,
};

[[nodiscard]] LibraryPickerPresentation makeLibraryPickerPresentation(
    const LibraryPickerSnapshot& snapshot,
    std::string selectedItemName = {},
    std::string errorMessage = {});

[[nodiscard]] std::string trimLibraryProjectName(std::string value);

[[nodiscard]] LibraryPickerActionLayout chooseLibraryPickerActionLayout(
    float availableWidth,
    const std::vector<float>& labelWidths,
    float horizontalFramePadding,
    float itemSpacing);

[[nodiscard]] bool isConfirmedLibraryDeletion(
    const std::vector<workshop::LibraryItemRef>& requested,
    const LibraryDeleteResult& result) noexcept;

} // namespace dw::design_library

#include "library_panel.h"

#include <algorithm>
#include <utility>

namespace dw {
namespace {

design_library::LibraryActionResult actionResult(
    const design_library::LibraryPanelIntentResult& result) {
    if (const auto* action = std::get_if<design_library::LibraryActionResult>(&result))
        return *action;
    return {design_library::LibraryActionResultStatus::Rejected,
            "The application returned the wrong result for this action."};
}

} // namespace

void LibraryPanel::setSelectedModelId(int64_t id) {
    m_selectedModelIds.clear();
    m_selectedGCodeIds.clear();
    m_lastClickedModelId = id;
    m_lastClickedGCodeId = -1;
    if (id > 0) {
        const workshop::LibraryItemRef item{
            workshop::LibraryItemKind::Model, workshop::LibraryItemId(id)};
        m_selectedModelIds.insert(id);
        m_pendingSelectionReveal = item;
    } else {
        m_pendingSelectionReveal.reset();
    }
}

void LibraryPanel::setPickerState(const design_library::LibraryPickerSnapshot& snapshot,
                                  std::string errorMessage) {
    const bool opening = snapshot.active && !m_pickerSnapshot.active;
    m_pickerSnapshot = snapshot;
    m_pickerError = std::move(errorMessage);
    if (opening) {
        m_pickerInputSuppressionFrames = 1;
        if (isProjectModelPicker())
            m_activeTab = ViewTab::Models;
    }
    if (m_pickerSnapshot.active)
        applyPickerSelection(m_pickerSnapshot.selectedItems);
}

void LibraryPanel::clearPickerState() {
    m_pickerSnapshot = {};
    m_pickerError.clear();
    m_pickerInputSuppressionFrames = 0;
    m_pendingSelectionReveal.reset();
}

design_library::LibraryPanelIntentResult LibraryPanel::emitLibraryIntent(
    const design_library::LibraryPanelIntent& intent) {
    if (!m_onLibraryIntent) {
        return design_library::LibraryActionResult{
            design_library::LibraryActionResultStatus::Rejected,
            "This Library action is not available."};
    }
    return m_onLibraryIntent(intent);
}

std::vector<workshop::LibraryItemRef> LibraryPanel::selectedLibraryItems() const {
    std::vector<workshop::LibraryItemRef> items;
    items.reserve(m_selectedModelIds.size() + m_selectedGCodeIds.size());
    for (const auto id : m_selectedModelIds) {
        items.push_back(
            {workshop::LibraryItemKind::Model, workshop::LibraryItemId(id)});
    }
    for (const auto id : m_selectedGCodeIds) {
        items.push_back(
            {workshop::LibraryItemKind::GCode, workshop::LibraryItemId(id)});
    }
    return items;
}

std::optional<workshop::LibraryItemRef> LibraryPanel::focusedLibraryItem() const {
    if (m_lastClickedModelId > 0 && m_selectedModelIds.count(m_lastClickedModelId) > 0) {
        return workshop::LibraryItemRef{workshop::LibraryItemKind::Model,
                                        workshop::LibraryItemId(m_lastClickedModelId)};
    }
    if (m_lastClickedGCodeId > 0 && m_selectedGCodeIds.count(m_lastClickedGCodeId) > 0) {
        return workshop::LibraryItemRef{workshop::LibraryItemKind::GCode,
                                        workshop::LibraryItemId(m_lastClickedGCodeId)};
    }
    const auto items = selectedLibraryItems();
    if (items.empty())
        return std::nullopt;
    return items.front();
}

std::string LibraryPanel::focusedLibraryItemName() const {
    const auto focused = focusedLibraryItem();
    if (!focused)
        return {};
    if (focused->kind == workshop::LibraryItemKind::Model) {
        const auto found = std::find_if(m_models.begin(), m_models.end(), [focused](const auto& item) {
            return item.id == focused->item.value;
        });
        return found == m_models.end() ? std::string{} : found->name;
    }
    const auto found =
        std::find_if(m_gcodeFiles.begin(), m_gcodeFiles.end(), [focused](const auto& item) {
            return item.id == focused->item.value;
        });
    return found == m_gcodeFiles.end() ? std::string{} : found->name;
}

void LibraryPanel::applyPickerSelection(
    const std::vector<workshop::LibraryItemRef>& items) {
    m_selectedModelIds.clear();
    m_selectedGCodeIds.clear();
    m_lastClickedModelId = -1;
    m_lastClickedGCodeId = -1;
    m_pendingSelectionReveal.reset();
    for (const auto item : items) {
        if (!item.valid())
            continue;
        if (item.kind == workshop::LibraryItemKind::Model) {
            m_selectedModelIds.insert(item.item.value);
            m_lastClickedModelId = item.item.value;
        } else {
            m_selectedGCodeIds.insert(item.item.value);
            m_lastClickedGCodeId = item.item.value;
        }
        m_pendingSelectionReveal = item;
    }
}

void LibraryPanel::emitSelectionChanged() {
    const auto result = actionResult(
        emitLibraryIntent(design_library::LibrarySelectionChanged{selectedLibraryItems()}));
    if (result.status == design_library::LibraryActionResultStatus::Rejected)
        m_pickerError = result.message;
    else
        m_pickerError.clear();
}

void LibraryPanel::emitPreviewRequested(workshop::LibraryItemRef item) {
    const auto result =
        actionResult(emitLibraryIntent(design_library::LibraryPreviewRequested{item}));
    if (result.status == design_library::LibraryActionResultStatus::Rejected)
        m_pickerError = result.message;
    else
        m_pickerError.clear();
}

bool LibraryPanel::isPickerProjectMember(workshop::LibraryItemRef item) const {
    return m_pickerSnapshot.active && m_pickerSnapshot.isProjectMember(item);
}

bool LibraryPanel::isProjectModelPicker() const noexcept {
    return m_pickerSnapshot.active &&
           m_pickerSnapshot.purpose !=
               design_library::LibraryPickerPurpose::ManageLibrary;
}

void LibraryPanel::applyConfirmedDeletion(
    const std::vector<workshop::LibraryItemRef>& confirmedItems) {
    for (const auto item : confirmedItems) {
        if (item.kind == workshop::LibraryItemKind::Model) {
            m_selectedModelIds.erase(item.item.value);
            if (m_lastClickedModelId == item.item.value)
                m_lastClickedModelId = -1;
        } else {
            m_selectedGCodeIds.erase(item.item.value);
            if (m_lastClickedGCodeId == item.item.value)
                m_lastClickedGCodeId = -1;
        }
    }
    refresh();
    emitSelectionChanged();
}

void LibraryPanel::renderPickerHeader() {
    const auto presentation = design_library::makeLibraryPickerPresentation(
        m_pickerSnapshot, focusedLibraryItemName(), m_pickerError);
    m_pickerView.render(
        presentation,
        design_library::LibraryPickerViewCallbacks{
            [this]() {
                const auto item = focusedLibraryItem();
                if (!item) {
                    return design_library::LibraryActionResult{
                        design_library::LibraryActionResultStatus::Rejected,
                        "Select an item to preview."};
                }
                return actionResult(
                    emitLibraryIntent(design_library::LibraryPreviewRequested{*item}));
            },
            [this](std::string projectName) {
                return actionResult(emitLibraryIntent(
                    design_library::LibraryPrimaryActionRequested{
                        m_pickerSnapshot.purpose, std::move(projectName)}));
            },
            [this]() {
                return actionResult(
                    emitLibraryIntent(design_library::LibraryCancelRequested{}));
            }});
}

} // namespace dw

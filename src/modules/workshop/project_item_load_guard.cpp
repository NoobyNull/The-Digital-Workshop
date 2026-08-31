#include "project_item_load_guard.h"

namespace dw::workshop {

bool projectItemLoadCanCommit(const WorkshopContextSnapshot& context,
                              ProjectItemRef expectedItem) noexcept {
    return expectedItem.valid() && context.route == WorkshopRoute::Project &&
           !context.libraryPreview.has_value() && context.activeProjectItem.has_value() &&
           *context.activeProjectItem == expectedItem;
}

bool libraryPreviewLoadCanCommit(const WorkshopContextSnapshot& context,
                                 LibraryItemRef expectedPreview) noexcept {
    return expectedPreview.valid() && context.route == WorkshopRoute::DesignLibrary &&
           context.libraryPreview.has_value() &&
           context.libraryPreview->kind == expectedPreview.kind &&
           context.libraryPreview->item == expectedPreview.item;
}

} // namespace dw::workshop

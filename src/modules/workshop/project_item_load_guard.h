#pragma once

#include "workshop_contract.h"

namespace dw::workshop {

// Async model results may commit only while the exact project item still owns
// the visible Project route. Library preview intentionally preserves the item
// identity, so route and preview state are part of this guard.
[[nodiscard]] bool projectItemLoadCanCommit(const WorkshopContextSnapshot& context,
                                            ProjectItemRef expectedItem) noexcept;

[[nodiscard]] bool libraryPreviewLoadCanCommit(const WorkshopContextSnapshot& context,
                                               LibraryItemRef expectedPreview) noexcept;

} // namespace dw::workshop

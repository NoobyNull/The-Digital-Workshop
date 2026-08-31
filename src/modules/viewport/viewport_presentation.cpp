#include "viewport_presentation.h"

#include <utility>

namespace dw::viewport {

namespace {
std::string joinedLabel(const std::string& context, const std::string& item) {
    if (context.empty()) {
        return item;
    }
    if (item.empty()) {
        return context;
    }
    return context + "  /  " + item;
}
} // namespace

PresentationIdentity::PresentationIdentity(ContentIdentityKind kind,
                                           std::string contextLabel,
                                           std::string itemLabel)
    : m_kind(kind),
      m_contextLabel(std::move(contextLabel)),
      m_itemLabel(std::move(itemLabel)) {}

PresentationIdentity PresentationIdentity::none() {
    return PresentationIdentity(ContentIdentityKind::None, {}, {});
}

PresentationIdentity PresentationIdentity::projectItem(std::string projectLabel,
                                                       std::string itemLabel) {
    return PresentationIdentity(ContentIdentityKind::ProjectItem,
                                std::move(projectLabel),
                                std::move(itemLabel));
}

PresentationIdentity PresentationIdentity::libraryPreview(std::string contextLabel,
                                                          std::string itemLabel) {
    return PresentationIdentity(ContentIdentityKind::LibraryPreview,
                                std::move(contextLabel),
                                std::move(itemLabel));
}

IdentityOverlayPresentation presentIdentity(const PresentationIdentity& identity) {
    IdentityOverlayPresentation view;
    if (identity.kind() == ContentIdentityKind::None || identity.itemLabel().empty()) {
        return view;
    }

    view.visible = true;
    view.label = joinedLabel(identity.contextLabel(), identity.itemLabel());
    if (identity.kind() == ContentIdentityKind::LibraryPreview) {
        view.badge = "Library preview";
        view.accessibleText = "Library preview: " + view.label;
    } else {
        view.badge = "Project item";
        view.accessibleText = "Project item: " + view.label;
    }
    return view;
}

} // namespace dw::viewport

#pragma once

#include <string>

namespace dw::viewport {

enum class ContentIdentityKind {
    None,
    ProjectItem,
    LibraryPreview,
};

// Immutable presentation input supplied by the application boundary. It says
// what the viewport is showing, without teaching rendering how projects or the
// Library choose, restore, or persist that content.
class PresentationIdentity {
  public:
    static PresentationIdentity none();
    static PresentationIdentity projectItem(std::string projectLabel,
                                            std::string itemLabel);
    static PresentationIdentity libraryPreview(std::string contextLabel,
                                               std::string itemLabel);

    [[nodiscard]] ContentIdentityKind kind() const noexcept { return m_kind; }
    [[nodiscard]] const std::string& contextLabel() const noexcept {
        return m_contextLabel;
    }
    [[nodiscard]] const std::string& itemLabel() const noexcept { return m_itemLabel; }

  private:
    PresentationIdentity(ContentIdentityKind kind,
                         std::string contextLabel,
                         std::string itemLabel);

    ContentIdentityKind m_kind = ContentIdentityKind::None;
    std::string m_contextLabel;
    std::string m_itemLabel;
};

struct IdentityOverlayPresentation {
    bool visible = false;
    std::string badge;
    std::string label;
    std::string accessibleText;
};

[[nodiscard]] IdentityOverlayPresentation
presentIdentity(const PresentationIdentity& identity);

} // namespace dw::viewport

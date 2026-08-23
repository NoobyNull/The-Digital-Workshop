#pragma once

#include <string>

namespace dw {

// Placeholder occupying the "direct_carve" window ID while the CAM
// workspace is rebuilt on the PureCutCNC engine (v0.8.0 milestone).
// See .planning/CAM-INTEGRATION-DESIGN.md.
class CamPlaceholderPanel {
  public:
    void render();
    [[nodiscard]] bool isVisible() const noexcept { return m_visible; }
    void setVisible(bool visible) noexcept { m_visible = visible; }
    [[nodiscard]] const std::string& statusCopy() const noexcept;

  private:
    bool m_visible = false;
};

} // namespace dw

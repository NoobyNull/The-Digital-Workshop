#pragma once

#include <string>

#include "ui/panels/panel.h"

namespace dw {

// Placeholder occupying the "direct_carve" window ID while the CAM
// workspace is rebuilt on the PureCutCNC engine (v0.8.0 milestone).
// See .planning/CAM-INTEGRATION-DESIGN.md.
class CamPlaceholderPanel : public Panel {
  public:
    CamPlaceholderPanel() : Panel("CAM") {}
    void render() override;
    [[nodiscard]] const std::string& statusCopy() const noexcept;
};

} // namespace dw

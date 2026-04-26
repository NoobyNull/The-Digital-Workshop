#pragma once

#include <imgui.h>

#include "core/cnc/cnc_tool.h"

namespace dw {

void renderToolProfilePreview(const VtdbToolGeometry& geometry, ImVec2 size);

} // namespace dw

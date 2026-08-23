#pragma once

#include <nlohmann/json_fwd.hpp>

namespace dw {

struct VtdbToolGeometry;

// Stable tool snapshot shared by the operation and its role-specific children.
nlohmann::json directCarveToolSummaryJson(const VtdbToolGeometry& tool);

} // namespace dw

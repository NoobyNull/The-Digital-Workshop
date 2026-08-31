#pragma once

#include <string>

namespace dw::cam {

// Inputs for a default surfacing job: the engine's rough + finish surface
// pipeline over one mesh. Tool geometry/feeds mirror the bridge's example
// spec until the interpreter (Phase 4) projects real .vtdb tools.
struct CamJobRequest {
    std::string modelName;
    std::string meshPath; // absolute path readable by the bridge host
    std::string machineId = "fluidnc";
};

// Pure builder: JobSpec JSON for POST /api/job. No outputPath — the G-code
// returns inline and DW persists it as a project item.
[[nodiscard]] std::string buildDefaultSurfacingJobSpec(const CamJobRequest& request);

} // namespace dw::cam

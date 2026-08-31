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
    std::string axisSwap = "none"; // engine mesh orientation: none|yz|xz|xy
};

// Pure builder: JobSpec JSON for POST /api/job. No outputPath — the G-code
// returns inline and DW persists it as a project item.
[[nodiscard]] std::string buildDefaultSurfacingJobSpec(const CamJobRequest& request);

// Orientation that lays the model flat for top-down carving: the axis swap
// (none|yz|xz) that minimizes the resulting Z height, from raw mesh extents.
[[nodiscard]] std::string layFlatAxisSwap(double extentX, double extentY, double extentZ);

} // namespace dw::cam

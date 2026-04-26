#pragma once

#include "heightmap.h"
#include "island_detector.h"
#include "toolpath_types.h"
#include "../cnc/cnc_tool.h"

#include <string>
#include <vector>

namespace dw {
namespace carve {

class ToolpathGenerator {
  public:
    // Generate finishing toolpath from heightmap with tool offset compensation
    Toolpath generateFinishing(const Heightmap& heightmap,
                               const ToolpathConfig& config,
                               f32 toolTipDiameter,
                               const VtdbToolGeometry& tool);

    // Generate clearing toolpath for islands only
    Toolpath generateClearing(const Heightmap& heightmap,
                              const IslandResult& islands,
                              const ToolpathConfig& config,
                              f32 toolDiameter);

    // Generate a fixed-depth 2.5D raster from top-zero work coordinates.
    Toolpath generateFixedDepthRaster(const Vec3& boundsMin,
                                      const Vec3& boundsMax,
                                      const ToolpathConfig& config,
                                      f32 toolDiameter,
                                      f32 depthMm);

    // Generate fixed-depth clearing over stock while avoiding the model
    // footprint expanded by the selected cutter radius.
    Toolpath generateFixedDepthClearingAroundModel(const Vec3& stockMin,
                                                   const Vec3& stockMax,
                                                   const Vec3& modelMin,
                                                   const Vec3& modelMax,
                                                   const ToolpathConfig& config,
                                                   f32 toolDiameter,
                                                   f32 depthMm);

    // Validate toolpath against machine travel limits, return warnings
    std::vector<std::string> validateLimits(
        const Toolpath& path,
        f32 travelX, f32 travelY, f32 travelZ) const;

  private:
    void generateScanLines(Toolpath& path,
                           const Heightmap& heightmap,
                           const ToolpathConfig& config,
                           f32 stepoverMm,
                           bool primaryAxis);  // true=X, false=Y
    void generateFixedDepthScanLines(Toolpath& path,
                                     const Vec3& boundsMin,
                                     const Vec3& boundsMax,
                                     const ToolpathConfig& config,
                                     f32 stepoverMm,
                                     f32 cutZ,
                                     bool primaryAxis);
    void generateFixedDepthClearingScanLines(Toolpath& path,
                                             const Vec3& stockMin,
                                             const Vec3& stockMax,
                                             const Vec3& noCutMin,
                                             const Vec3& noCutMax,
                                             bool hasNoCutRegion,
                                             const ToolpathConfig& config,
                                             f32 stepoverMm,
                                             f32 cutZ,
                                             bool primaryAxis);

    void addRetract(Toolpath& path, f32 safeZ);
    void addRapidTo(Toolpath& path, const Vec3& pos);
    void addCutTo(Toolpath& path, const Vec3& pos);

    void computeMetrics(Toolpath& path, const ToolpathConfig& config);

    // Tool offset compensation: compute Z offset for tool geometry at XY
    f32 toolOffset(const Heightmap& heightmap,
                   f32 x, f32 y,
                   const VtdbToolGeometry& tool) const;

    // Type-specific offset helpers
    f32 vBitOffset(const Heightmap& heightmap,
                   f32 x, f32 y,
                   const VtdbToolGeometry& tool) const;

    f32 ballNoseOffset(const Heightmap& heightmap,
                       f32 x, f32 y,
                       const VtdbToolGeometry& tool) const;

    f32 endMillOffset(const Heightmap& heightmap,
                      f32 x, f32 y,
                      const VtdbToolGeometry& tool) const;

    // Clear a single island region with depth-pass raster scanning
    void clearIslandRegion(Toolpath& path,
                           const Heightmap& heightmap,
                           const IslandResult& islands,
                           const Island& island,
                           const ToolpathConfig& config,
                           f32 stepoverMm,
                           f32 stepdownMm,
                           f32 toolRadius);
};

} // namespace carve
} // namespace dw

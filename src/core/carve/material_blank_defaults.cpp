#include "core/carve/material_blank_defaults.h"

#include <algorithm>

namespace dw::carve {

StockDimensions materialBlankFromModelBounds(const Vec3& boundsMin, const Vec3& boundsMax) {
    return {
        std::max(boundsMax.x - boundsMin.x, 1.0f),
        std::max(boundsMax.y - boundsMin.y, 1.0f),
        std::max(boundsMax.z - boundsMin.z, 0.5f),
    };
}

} // namespace dw::carve

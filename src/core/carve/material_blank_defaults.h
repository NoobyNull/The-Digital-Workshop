#pragma once

#include "core/carve/model_fitter.h"
#include "core/types.h"

namespace dw::carve {

StockDimensions materialBlankFromModelBounds(const Vec3& boundsMin, const Vec3& boundsMax);

} // namespace dw::carve

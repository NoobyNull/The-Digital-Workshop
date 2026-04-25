#pragma once

#include "heightmap.h"
#include "../types.h"

#include <vector>

namespace dw {
namespace carve {

std::vector<u8> generateHeightmapPreviewPixels(const Heightmap& heightmap);

} // namespace carve
} // namespace dw

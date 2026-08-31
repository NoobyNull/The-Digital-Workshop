#pragma once

#include <optional>

#include "../types.h"

namespace dw::image_thumbnail {

std::optional<Path> writeCachedTgaFromImage(i64 modelId, const Path& imagePath);

} // namespace dw::image_thumbnail

#pragma once

#include <optional>
#include <string>

#include "../types.h"

namespace dw {

bool isSupportedThumbnailImageExtension(std::string extension);
std::optional<Path> findSidecarThumbnailForImport(const Path& modelPath);

} // namespace dw

#pragma once

#include "../config/config.h"
#include "filesystem_detector.h"

namespace dw {
namespace import_mode_policy {

// Choose the initial import mode from saved preference and source location.
// Network imports never default to Move because interruption can risk data loss.
FileHandlingMode initialModeFor(StorageLocation location, FileHandlingMode savedMode);

} // namespace import_mode_policy
} // namespace dw

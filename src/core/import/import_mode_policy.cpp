#include "import_mode_policy.h"

namespace dw {
namespace import_mode_policy {

FileHandlingMode initialModeFor(StorageLocation location, FileHandlingMode savedMode) {
    if (location == StorageLocation::Network && savedMode == FileHandlingMode::MoveToLibrary) {
        return FileHandlingMode::CopyToLibrary;
    }
    return savedMode;
}

} // namespace import_mode_policy
} // namespace dw

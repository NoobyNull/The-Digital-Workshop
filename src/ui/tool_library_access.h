#pragma once

namespace dw {

inline constexpr const char* kToolLibraryWindowTitle = "Tool Library";
inline constexpr const char* kToolLibraryMenuLabel = "Tool Library";
inline constexpr const char* kToolLibraryStatusButtonLabel = "Tools";
inline constexpr const char* kToolLibraryStatusTooltip = "Open Tool Library / Toolbox";

inline bool toolLibraryStatusButtonVisible(bool loadingActive, bool importActive) {
    return !loadingActive && !importActive;
}

} // namespace dw

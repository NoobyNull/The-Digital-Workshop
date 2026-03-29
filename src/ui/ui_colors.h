#pragma once

#include <imgui.h>

namespace dw::colors {

// Semantic status colors used across all panels.
// These are the canonical definitions -- panels must not define their own copies.

// Error / alarm / failure indicators
inline constexpr ImVec4 kError{1.0f, 0.3f, 0.3f, 1.0f};

// Slightly softer red used for error text in table rows and summaries
inline constexpr ImVec4 kErrorText{1.0f, 0.4f, 0.4f, 1.0f};

// Success / OK / complete indicators
inline constexpr ImVec4 kSuccess{0.3f, 0.8f, 0.3f, 1.0f};

// Warning / caution / hold indicators
inline constexpr ImVec4 kWarning{1.0f, 0.8f, 0.2f, 1.0f};

// Informational / active / blue accent
inline constexpr ImVec4 kInfo{0.4f, 0.7f, 1.0f, 1.0f};

// Dimmed / disabled / secondary text
inline constexpr ImVec4 kDimmed{0.5f, 0.5f, 0.5f, 1.0f};

// Orange accent — used for caution warnings that are not full kWarning yellow
inline constexpr ImVec4 kOrange{1.0f, 0.5f, 0.0f, 1.0f};

// Axis colors for DRO / coordinate displays
inline constexpr ImVec4 kAxisX{1.0f, 0.3f, 0.3f, 1.0f};
inline constexpr ImVec4 kAxisY{0.3f, 1.0f, 0.3f, 1.0f};
inline constexpr ImVec4 kAxisZ{0.3f, 0.5f, 1.0f, 1.0f};

// CNC machine state colors (used in state badge and DRO indicators)
inline constexpr ImVec4 kStateRun{0.3f, 0.5f, 1.0f, 1.0f};   // Blue
inline constexpr ImVec4 kStateJog{0.3f, 0.7f, 1.0f, 1.0f};   // Light blue
inline constexpr ImVec4 kStateDoor{1.0f, 0.5f, 0.2f, 1.0f};  // Orange
inline constexpr ImVec4 kStateCheck{0.6f, 0.6f, 0.8f, 1.0f}; // Lavender
inline constexpr ImVec4 kStateHome{0.5f, 0.8f, 1.0f, 1.0f};  // Cyan

} // namespace dw::colors

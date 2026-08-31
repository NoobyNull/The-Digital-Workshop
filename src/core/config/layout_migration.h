#pragma once

#include <vector>

#include "core/config/layout_preset.h"

namespace dw {

inline constexpr int CURRENT_LAYOUT_MIGRATION_VERSION = 1;

// Immutable input snapshot for the versioned built-in layout migration.
struct LayoutMigrationSnapshot {
    int version = 0;
    int activePresetIndex = 0;
    std::vector<LayoutPreset> presets;
};

// Explicit output: callers persist only when changed is true. A disabled
// migration (current or future version) returns the snapshot unchanged.
struct LayoutMigrationResult {
    int version = 0;
    int activePresetIndex = 0;
    std::vector<LayoutPreset> presets;
    bool changed = false;
};

[[nodiscard]] LayoutMigrationResult migrateGuidedLayouts(const LayoutMigrationSnapshot& snapshot);

[[nodiscard]] bool isGuidedLayout(const LayoutPreset& preset) noexcept;
[[nodiscard]] bool isAdvancedLayout(const LayoutPreset& preset) noexcept;
[[nodiscard]] bool isCncLayout(const LayoutPreset& preset) noexcept;

} // namespace dw

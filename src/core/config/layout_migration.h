#pragma once

#include <vector>

#include "core/config/layout_preset.h"

namespace dw {

// Version 1 introduced stable built-in ids. Version 2 folds the retired Guided
// Workshop built-in into Workshop.
inline constexpr int CURRENT_LAYOUT_MIGRATION_VERSION = 2;

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

[[nodiscard]] LayoutMigrationResult migrateLayoutPresets(const LayoutMigrationSnapshot& snapshot);

[[nodiscard]] bool isWorkshopLayout(const LayoutPreset& preset) noexcept;
[[nodiscard]] bool isCncLayout(const LayoutPreset& preset) noexcept;

} // namespace dw

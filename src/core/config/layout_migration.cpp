#include "core/config/layout_migration.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace dw {
namespace {

enum class BuiltInKind {
    Guided,
    Advanced,
    Cnc,
};

bool matches(std::string_view value,
             std::string_view canonical,
             std::initializer_list<std::string_view> aliases) noexcept {
    if (value == canonical)
        return true;
    return std::find(aliases.begin(), aliases.end(), value) != aliases.end();
}

std::optional<BuiltInKind> builtInKind(const LayoutPreset& preset) noexcept {
    if (!preset.builtIn)
        return std::nullopt;

    if (matches(preset.id, GUIDED_LAYOUT_ID, {"guided", "workshop_guided"}) ||
        matches(preset.name, "Guided Workshop", {"Guided"})) {
        return BuiltInKind::Guided;
    }
    if (matches(preset.id, ADVANCED_LAYOUT_ID, {"advanced", "model", "workshop"}) ||
        matches(preset.name, "Advanced Workbench", {"Workshop", "Modeling", "Model Workshop"})) {
        return BuiltInKind::Advanced;
    }
    if (matches(preset.id, CNC_LAYOUT_ID, {"cnc", "sender"}) ||
        matches(preset.name, "CNC Sender", {"Sender"})) {
        return BuiltInKind::Cnc;
    }
    return std::nullopt;
}

int canonicalIndex(BuiltInKind kind) noexcept {
    switch (kind) {
    case BuiltInKind::Guided:
        return 0;
    case BuiltInKind::Advanced:
        return 1;
    case BuiltInKind::Cnc:
        return 2;
    }
    return 1;
}

} // namespace

bool isGuidedLayout(const LayoutPreset& preset) noexcept {
    return preset.id == GUIDED_LAYOUT_ID;
}

bool isAdvancedLayout(const LayoutPreset& preset) noexcept {
    return preset.id == ADVANCED_LAYOUT_ID;
}

bool isCncLayout(const LayoutPreset& preset) noexcept {
    return preset.id == CNC_LAYOUT_ID;
}

LayoutMigrationResult migrateGuidedLayouts(const LayoutMigrationSnapshot& snapshot) {
    if (snapshot.version >= CURRENT_LAYOUT_MIGRATION_VERSION) {
        return {snapshot.version, snapshot.activePresetIndex, snapshot.presets, false};
    }

    LayoutMigrationResult result;
    result.version = CURRENT_LAYOUT_MIGRATION_VERSION;
    result.presets = {
        LayoutPreset::guidedDefault(),
        LayoutPreset::advancedDefault(),
        LayoutPreset::cncDefault(),
    };
    result.changed = true;

    std::optional<BuiltInKind> activeBuiltIn;
    int activeCustomOrdinal = -1;
    int customOrdinal = 0;
    for (std::size_t index = 0; index < snapshot.presets.size(); ++index) {
        const auto& preset = snapshot.presets[index];
        const auto kind = builtInKind(preset);
        if (static_cast<int>(index) == snapshot.activePresetIndex) {
            if (kind)
                activeBuiltIn = kind;
            else
                activeCustomOrdinal = customOrdinal;
        }

        if (!kind) {
            // Preserve the custom object exactly: no key canonicalization,
            // builtIn coercion, renaming, or visibility completion.
            result.presets.push_back(preset);
            ++customOrdinal;
        }
    }

    if (activeBuiltIn) {
        result.activePresetIndex = canonicalIndex(*activeBuiltIn);
    } else if (activeCustomOrdinal >= 0) {
        result.activePresetIndex = 3 + activeCustomOrdinal;
    } else {
        // Existing installs remain on the freeform workbench. Guided becomes
        // the release default only after the separate acceptance gate.
        result.activePresetIndex = 1;
    }
    return result;
}

} // namespace dw

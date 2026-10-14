#include "core/config/layout_migration.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace dw {
namespace {

enum class BuiltInKind {
    Workshop,
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

    // The Guided Workshop and Advanced Workbench built-ins of earlier releases
    // both become Workshop.
    if (matches(preset.id,
                WORKSHOP_LAYOUT_ID,
                {"advanced_workbench", "advanced", "model", "guided_workshop", "guided",
                 "workshop_guided"}) ||
        matches(preset.name,
                "Workshop",
                {"Advanced Workbench", "Guided Workshop", "Guided", "Modeling",
                 "Model Workshop"})) {
        return BuiltInKind::Workshop;
    }
    if (matches(preset.id, CNC_LAYOUT_ID, {"cnc", "sender"}) ||
        matches(preset.name, "CNC Sender", {"Sender"})) {
        return BuiltInKind::Cnc;
    }
    return std::nullopt;
}

constexpr int kBuiltInCount = 2;

int canonicalIndex(BuiltInKind kind) noexcept {
    switch (kind) {
    case BuiltInKind::Workshop:
        return 0;
    case BuiltInKind::Cnc:
        return 1;
    }
    return 0;
}

} // namespace

bool isWorkshopLayout(const LayoutPreset& preset) noexcept {
    return preset.id == WORKSHOP_LAYOUT_ID;
}

bool isCncLayout(const LayoutPreset& preset) noexcept {
    return preset.id == CNC_LAYOUT_ID;
}

LayoutMigrationResult migrateLayoutPresets(const LayoutMigrationSnapshot& snapshot) {
    if (snapshot.version >= CURRENT_LAYOUT_MIGRATION_VERSION) {
        return {snapshot.version, snapshot.activePresetIndex, snapshot.presets, false};
    }

    LayoutMigrationResult result;
    result.version = CURRENT_LAYOUT_MIGRATION_VERSION;
    result.presets = {
        LayoutPreset::workshopDefault(),
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
        result.activePresetIndex = kBuiltInCount + activeCustomOrdinal;
    } else {
        result.activePresetIndex = canonicalIndex(BuiltInKind::Workshop);
    }
    return result;
}

} // namespace dw

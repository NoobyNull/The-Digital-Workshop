#pragma once

#include "toolpath_types.h"

#include <cmath>
#include <optional>
#include <string>

namespace dw::carve {

struct ToolpathRuntimeAdvice {
    bool warn = false;
    StepoverPreset suggestedPreset = StepoverPreset::Fine;
    f32 suggestedPercent = 0.0f;
    f32 estimatedSeconds = 0.0f;
    int estimatedLines = 0;
    bool reachesTarget = false;
};

inline const char* stepoverPresetShortLabel(StepoverPreset preset)
{
    switch (preset) {
        case StepoverPreset::UltraFine: return "Ultra Fine";
        case StepoverPreset::Fine: return "Fine";
        case StepoverPreset::Basic: return "Basic";
        case StepoverPreset::Rough: return "Rough";
        case StepoverPreset::Roughing: return "Roughing";
    }
    return "Basic";
}

inline std::optional<ToolpathRuntimeAdvice> adviseToolpathRuntime(
    const ToolpathConfig& config,
    f32 totalTimeSec,
    int totalLineCount,
    f32 warnTimeSec = 12.0f * 60.0f * 60.0f,
    int warnLineCount = 1000000)
{
    if (totalTimeSec < warnTimeSec && totalLineCount < warnLineCount) {
        return std::nullopt;
    }

    const f32 currentPct = config.customStepoverPct > 0.0f
        ? config.customStepoverPct
        : stepoverPercent(config.stepoverPreset);
    if (currentPct <= 0.0f) {
        return std::nullopt;
    }

    constexpr StepoverPreset kPresets[] = {
        StepoverPreset::Fine,
        StepoverPreset::Basic,
        StepoverPreset::Rough,
        StepoverPreset::Roughing,
    };

    ToolpathRuntimeAdvice best;
    best.warn = true;

    const f32 targetRatio = totalTimeSec > warnTimeSec && warnTimeSec > 0.0f
        ? std::sqrt(totalTimeSec / warnTimeSec)
        : 1.0f;
    const f32 minimumPercent = currentPct * targetRatio;

    for (StepoverPreset preset : kPresets) {
        const f32 pct = stepoverPercent(preset);
        if (pct <= currentPct || pct < minimumPercent) {
            continue;
        }

        const f32 ratio = currentPct / pct;
        best.suggestedPreset = preset;
        best.suggestedPercent = pct;
        best.estimatedSeconds = totalTimeSec * ratio * ratio;
        best.estimatedLines = static_cast<int>(
            std::ceil(static_cast<f32>(std::max(totalLineCount, 0)) * ratio * ratio));
        best.reachesTarget = best.estimatedSeconds <= warnTimeSec;
        return best;
    }

    const f32 pct = stepoverPercent(StepoverPreset::Roughing);
    const f32 ratio = currentPct / pct;
    best.suggestedPreset = StepoverPreset::Roughing;
    best.suggestedPercent = pct;
    best.estimatedSeconds = totalTimeSec * ratio * ratio;
    best.estimatedLines = static_cast<int>(
        std::ceil(static_cast<f32>(std::max(totalLineCount, 0)) * ratio * ratio));
    best.reachesTarget = best.estimatedSeconds <= warnTimeSec;
    return best;
}

} // namespace dw::carve

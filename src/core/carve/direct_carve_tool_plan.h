#pragma once

#include "../cnc/cnc_tool.h"

#include <optional>
#include <string>
#include <string_view>

namespace dw::carve {

// The two independent tool slots presented by Direct Carve. A picker action
// always names its destination so choosing one tool cannot replace the other.
enum class DirectCarveToolPickerRole {
    Finishing,
    Clearing,
};

// Automatic preserves the legacy behavior: Direct Carve may use the supplied
// roughing recommendation. Selected means the user's explicit clearing intent.
enum class ClearingToolMode {
    Automatic,
    Selected,
    Disabled,
};

enum class DirectCarveToolSelectionError {
    None,
    IncompleteToolGeometry,
    UnsupportedClearingTool,
};

struct DirectCarveToolSelectionResult {
    DirectCarveToolSelectionError error = DirectCarveToolSelectionError::None;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == DirectCarveToolSelectionError::None;
    }
};

// UUID is the authoritative identity. Manually entered tools do not have one,
// so their physical geometry forms a deterministic fallback identity.
[[nodiscard]] std::string stableDirectCarveToolIdentity(const VtdbToolGeometry& tool);
[[nodiscard]] bool sameDirectCarveToolIdentity(const VtdbToolGeometry& lhs,
                                               const VtdbToolGeometry& rhs);

[[nodiscard]] bool isSupportedClearingTool(const VtdbToolGeometry& tool);
[[nodiscard]] bool isUsableDirectCarveTool(const VtdbToolGeometry& tool);

[[nodiscard]] VtdbToolGeometry makeDirectCarveManualTool(
    VtdbToolType type, f64 diameterMm, int fluteCount, f64 includedAngleDeg, f64 tipRadiusMm);

[[nodiscard]] std::string_view clearingToolModeKey(ClearingToolMode mode) noexcept;

// Empty or unrecognized persisted values come from the legacy single-tool
// format and intentionally retain its automatic-clearing behavior.
[[nodiscard]] ClearingToolMode parseClearingToolModeKey(std::string_view key) noexcept;

class DirectCarveToolPlan {
  public:
    [[nodiscard]] const std::optional<VtdbToolGeometry>& finishingIntent() const noexcept;
    [[nodiscard]] const std::optional<VtdbToolGeometry>& clearingIntent() const noexcept;
    [[nodiscard]] const std::optional<VtdbToolGeometry>& effectiveClearingTool() const noexcept;
    [[nodiscard]] ClearingToolMode clearingMode() const noexcept;
    [[nodiscard]] bool selectionComplete() const noexcept;

    [[nodiscard]] DirectCarveToolSelectionResult selectTool(DirectCarveToolPickerRole role,
                                                            const VtdbToolGeometry& tool);
    void clearTool(DirectCarveToolPickerRole role);
    void setClearingMode(ClearingToolMode mode);

    // Resolves intent only. It does not claim that a clearing pass was emitted.
    [[nodiscard]] std::optional<VtdbToolGeometry> resolveClearingIntent(
        const std::optional<VtdbToolGeometry>& automaticRecommendation) const;

    // Generation owns the effective result. A tool becomes effective only
    // after a nonempty clearing pass has actually been produced.
    [[nodiscard]] bool confirmEffectiveClearing(const std::optional<VtdbToolGeometry>& usedTool,
                                                bool generatedNonemptyClearingPass);
    void invalidateEffectiveClearing() noexcept;

  private:
    std::optional<VtdbToolGeometry> m_finishingIntent;
    ClearingToolMode m_clearingMode = ClearingToolMode::Automatic;
    std::optional<VtdbToolGeometry> m_clearingIntent;
    std::optional<VtdbToolGeometry> m_effectiveClearingTool;
};

} // namespace dw::carve

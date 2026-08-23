#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "prepare_carve_flow.h"

namespace dw::carve_preparation {

enum class PreparationStepField {
    DesignWidth,
    DesignHeight,
    DesignDepth,
    DesignScale,
    BlankWidth,
    BlankLength,
    BlankThickness,
    CutDepth,
    Stepover,
    FeedRate,
    SpindleSpeed,
};

enum class PreparationStepSelection {
    FitMode,
    Material,
    FinishingTool,
    ClearingTool,
    ToolpathStrategy,
};

struct PreparationOptionId {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return value > 0; }

    friend constexpr bool operator==(PreparationOptionId lhs,
                                     PreparationOptionId rhs) noexcept {
        return lhs.value == rhs.value;
    }
};

struct OpenPreparationField {
    PrepareCarvePin pin;
    PreparationStageId stage;
    PreparationStepField field;
};

using PreparationFieldValue = std::variant<double, bool>;

struct UpdatePreparationField {
    PrepareCarvePin pin;
    PreparationStageId stage;
    PreparationStepField field;
    PreparationFieldValue value;
};

struct SelectPreparationOption {
    PrepareCarvePin pin;
    PreparationStageId stage;
    PreparationStepSelection selection;
    PreparationOptionId option;
};

struct RequestStepPreviewGeneration {
    PrepareCarvePin pin;
};

struct TogglePreparationAdvanced {
    PrepareCarvePin pin;
    PreparationStageId stage;
    bool expanded = false;
};

using PreparationStepIntent = std::variant<OpenPreparationField,
                                           UpdatePreparationField,
                                           SelectPreparationOption,
                                           RequestStepPreviewGeneration,
                                           TogglePreparationAdvanced>;

class PreparationAdvancedState final {
  public:
    [[nodiscard]] bool expanded(PreparationStageId stage) const noexcept;
    [[nodiscard]] PreparationAdvancedState withExpansion(
        PreparationStageId stage,
        bool expanded) const noexcept;

  private:
    std::array<bool, 4> m_expanded{};
};

struct PreparationRecommendationFact {
    std::string label;
    std::string reason;
};

// One immutable, identity-pinned view of the facts needed to render a step.
// The presenter cannot refresh repositories or substitute the active project.
class PreparationStepFacts final {
  public:
    PreparationStepFacts(
        PrepareCarvePin pin,
        PreparationStageProjection stage,
        PreparationAdvancedState advanced = {},
        std::optional<PreparationRecommendationFact> recommendation = std::nullopt);

    [[nodiscard]] const PrepareCarvePin& pin() const noexcept;
    [[nodiscard]] const PreparationStageProjection& stage() const noexcept;
    [[nodiscard]] const PreparationAdvancedState& advanced() const noexcept;
    [[nodiscard]] const std::optional<PreparationRecommendationFact>&
    recommendation() const noexcept;

  private:
    PrepareCarvePin m_pin;
    PreparationStageProjection m_stage;
    PreparationAdvancedState m_advanced;
    std::optional<PreparationRecommendationFact> m_recommendation;
};

enum class PreparationPrimaryActionKind {
    None,
    Continue,
    GeneratePreview,
};

struct PreparationPrimaryActionPresentation {
    PreparationPrimaryActionKind kind = PreparationPrimaryActionKind::None;
    std::string label;
    bool available = false;
};

struct PreparationAdvancedPresentation {
    bool available = true;
    bool expanded = false;
    std::string label;
};

struct PreparationStepPresentation {
    PreparationStageId stage = PreparationStageId::DesignAndSize;
    std::string title;
    std::string whyItMatters;
    std::string nextGuidance;
    std::string recommendationLabel;
    std::string recommendationRationale;
    PreparationPrimaryActionPresentation primaryAction;
    PreparationAdvancedPresentation advanced;
};

struct PreparationStepGuidanceOptions {
    bool enabled = true;
    bool advancedAvailable = true;
};

[[nodiscard]] PreparationStepPresentation buildPreparationStepPresentation(
    const PreparationStepFacts& facts,
    PreparationStepGuidanceOptions options = {});

} // namespace dw::carve_preparation

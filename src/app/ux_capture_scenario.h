#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dw {

enum class UxCaptureScenario {
    GuidedHome,
    LibraryStartProject,
    LibraryPreview,
    ProjectPlan,
    PrepareDesignAndSize,
    PrepareMaterialAndBlank,
    PrepareChooseTool,
    PrepareCarvePreview,
    ReviewMissingRequirement,
    ReviewReady,
    RunStreaming,
    RunPausedAbortFocused,
};

struct UxCaptureScenarioDescriptor {
    UxCaptureScenario scenario = UxCaptureScenario::GuidedHome;
    int ordinal = 1;
    const char* name = "guided-home";
    const char* surface = "guided-home";
};

[[nodiscard]] const std::vector<UxCaptureScenarioDescriptor>&
uxCaptureScenarios();

[[nodiscard]] std::optional<UxCaptureScenarioDescriptor>
findUxCaptureScenario(const std::string& name);

} // namespace dw

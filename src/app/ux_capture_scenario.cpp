#include "app/ux_capture_scenario.h"

#include <algorithm>

namespace dw {

const std::vector<UxCaptureScenarioDescriptor>& uxCaptureScenarios() {
    static const std::vector<UxCaptureScenarioDescriptor> scenarios = {
        {UxCaptureScenario::GuidedHome, 1, "guided-home", "guided-home"},
        {UxCaptureScenario::LibraryStartProject, 2,
         "library-start-project", "library-start-project"},
        {UxCaptureScenario::LibraryPreview, 3,
         "library-preview", "library-preview"},
        {UxCaptureScenario::ProjectPlan, 4, "project-plan", "project-plan"},
        {UxCaptureScenario::PrepareDesignAndSize, 5,
         "prepare-design-size", "prepare-design-size"},
        {UxCaptureScenario::PrepareMaterialAndBlank, 6,
         "prepare-material-blank", "prepare-material-blank"},
        {UxCaptureScenario::PrepareChooseTool, 7,
         "prepare-choose-tool", "prepare-choose-tool"},
        {UxCaptureScenario::PrepareCarvePreview, 8,
         "prepare-carve-preview", "prepare-carve-preview"},
        {UxCaptureScenario::ReviewMissingRequirement, 9,
         "review-missing", "review-missing"},
        {UxCaptureScenario::ReviewReady, 10,
         "review-ready", "review-ready"},
        {UxCaptureScenario::RunStreaming, 11,
         "run-streaming", "run-streaming"},
        {UxCaptureScenario::RunPausedAbortFocused, 12,
         "run-paused-abort-focus", "run-paused-abort-focus"},
    };
    return scenarios;
}

std::optional<UxCaptureScenarioDescriptor>
findUxCaptureScenario(const std::string& name) {
    const auto& scenarios = uxCaptureScenarios();
    const auto found = std::find_if(
        scenarios.begin(), scenarios.end(),
        [&name](const UxCaptureScenarioDescriptor& descriptor) {
            return name == descriptor.name;
        });
    if (found == scenarios.end()) {
        return std::nullopt;
    }
    return *found;
}

} // namespace dw

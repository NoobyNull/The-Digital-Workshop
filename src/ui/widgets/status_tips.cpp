#include "status_tips.h"

#include <string>
#include <vector>

namespace dw {
namespace {

std::string bindingName(const InputBinding& binding, const char* fallback) {
    return binding.isValid() ? binding.displayName() : fallback;
}

} // namespace

std::vector<std::string> buildStatusTips(StatusTipContext context, const StatusTipState& state) {
    switch (context) {
    case StatusTipContext::WorkshopViewport: {
        std::vector<std::string> tips;
        tips.push_back("Viewport: hold " + bindingName(state.lightDirection, "the light binding") +
                       " and drag to move the light");
        tips.push_back("Viewport: hold " + bindingName(state.lightIntensity, "the intensity binding") +
                       " and drag up/down to adjust light intensity");
        tips.push_back(state.hasLoadedModel
                           ? "Viewport: right-click for view tools and Recalculate Normals"
                           : "Viewport: right-click for view tools");
        return tips;
    }
    case StatusTipContext::Sender: {
        std::vector<std::string> tips;
        if (state.cncStreaming) {
            tips.push_back("Device: the active stream keeps this tab open until the job stops");
        } else if (!state.cncConnected) {
            tips.push_back("Device: connect a controller before streaming G-code");
        } else {
            tips.push_back("Device: open G-code in Preview, then use Send to Machine");
        }
        tips.push_back("Device: " + bindingName(state.feedOverridePlus, "feed +") + " / " +
                       bindingName(state.feedOverrideMinus, "feed -") + " adjusts feed override");
        tips.push_back("Device: " + bindingName(state.spindleOverridePlus, "spindle +") + " / " +
                       bindingName(state.spindleOverrideMinus, "spindle -") +
                       " adjusts spindle override");
        return tips;
    }
    case StatusTipContext::Workshop:
    default:
        return {
            "Ctrl+I imports models into the Library",
            "Ctrl+1 to Ctrl+6 switch between the tabs",
            "Prepare: click a design in the project to load it",
        };
    }
}

} // namespace dw

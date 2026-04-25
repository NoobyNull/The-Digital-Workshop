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
            tips.push_back("Sender: active stream keeps Workshop locked until the job stops");
        } else if (!state.cncConnected) {
            tips.push_back("Sender: connect a controller before streaming G-code");
        } else {
            tips.push_back("Sender: load or resume G-code from the G-code Viewer");
        }
        tips.push_back("Sender: " + bindingName(state.feedOverridePlus, "feed +") + " / " +
                       bindingName(state.feedOverrideMinus, "feed -") + " adjusts feed override");
        tips.push_back("Sender: " + bindingName(state.spindleOverridePlus, "spindle +") + " / " +
                       bindingName(state.spindleOverrideMinus, "spindle -") +
                       " adjusts spindle override");
        return tips;
    }
    case StatusTipContext::Workshop:
    default:
        return {
            "Workshop: Ctrl+I imports models",
            "Workshop: select a model from the Library to load it",
            "Workshop: use Tools > Recalculate Model Normals for the loaded model",
        };
    }
}

} // namespace dw

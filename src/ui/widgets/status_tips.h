#pragma once

#include <string>
#include <vector>

#include "../../core/config/input_binding.h"

namespace dw {

enum class StatusTipContext {
    Workshop,
    WorkshopViewport,
    Sender,
};

struct StatusTipState {
    bool hasLoadedModel = false;
    bool cncConnected = false;
    bool cncStreaming = false;

    InputBinding lightDirection;
    InputBinding lightIntensity;
    InputBinding feedOverridePlus;
    InputBinding feedOverrideMinus;
    InputBinding spindleOverridePlus;
    InputBinding spindleOverrideMinus;
};

std::vector<std::string> buildStatusTips(StatusTipContext context, const StatusTipState& state);

} // namespace dw

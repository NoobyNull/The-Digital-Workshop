#pragma once

#include <string>

namespace dw {

class UnifiedSettingsMap;

namespace cnc {

enum class SendUnits {
    Millimeters,
    Inches
};

SendUnits sendUnitsFromReportInchesValue(const std::string& value);
SendUnits sendUnitsFromUnifiedSettings(const UnifiedSettingsMap& settings);

const char* gcodeUnitMode(SendUnits units);
const char* unitLabel(SendUnits units);
const char* feedUnitLabel(SendUnits units);

float toSendLength(float millimeters, SendUnits units);
float toSendFeed(float millimetersPerMinute, SendUnits units);

} // namespace cnc
} // namespace dw

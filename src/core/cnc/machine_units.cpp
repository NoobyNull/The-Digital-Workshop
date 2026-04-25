#include "machine_units.h"

#include "unified_settings.h"

namespace dw {
namespace cnc {

namespace {

constexpr float kMmPerInch = 25.4f;

} // namespace

SendUnits sendUnitsFromReportInchesValue(const std::string& value)
{
    if (value.empty()) {
        return SendUnits::Millimeters;
    }

    try {
        return std::stof(value) > 0.5f ? SendUnits::Inches : SendUnits::Millimeters;
    } catch (...) {
        return SendUnits::Millimeters;
    }
}

SendUnits sendUnitsFromUnifiedSettings(const UnifiedSettingsMap& settings)
{
    const auto* reportInches = settings.get("report_inches");
    if (!reportInches) {
        return SendUnits::Millimeters;
    }
    return sendUnitsFromReportInchesValue(reportInches->value);
}

const char* gcodeUnitMode(SendUnits units)
{
    return units == SendUnits::Inches ? "G20" : "G21";
}

const char* unitLabel(SendUnits units)
{
    return units == SendUnits::Inches ? "in" : "mm";
}

const char* feedUnitLabel(SendUnits units)
{
    return units == SendUnits::Inches ? "in/min" : "mm/min";
}

float toSendLength(float millimeters, SendUnits units)
{
    return units == SendUnits::Inches ? millimeters / kMmPerInch : millimeters;
}

float toSendFeed(float millimetersPerMinute, SendUnits units)
{
    return toSendLength(millimetersPerMinute, units);
}

} // namespace cnc
} // namespace dw

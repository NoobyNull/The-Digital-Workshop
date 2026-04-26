#include "tool_profile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <regex>
#include <string>

namespace dw {

namespace {

constexpr double kDefaultPreviewDiameterMm = 6.35;
constexpr const char* kMeasurementPattern =
    R"(((?:\d+(?:\.\d+)?\s*-\s*)?\d+(?:\.\d+)?(?:\s*/\s*\d+(?:\.\d+)?)?|\d*\.\d+))";
constexpr const char* kUnitPattern =
    R"(\s*(mm|millimeters?|millimetres?|cm|inches?|inch|in|"))";

bool isKnownToolType(VtdbToolType type) {
    switch (type) {
    case VtdbToolType::BallNose:
    case VtdbToolType::EndMill:
    case VtdbToolType::Radiused:
    case VtdbToolType::VBit:
    case VtdbToolType::TaperedBallNose:
    case VtdbToolType::Drill:
    case VtdbToolType::ThreadMill:
    case VtdbToolType::FormTool:
    case VtdbToolType::DiamondDrag:
        return true;
    default:
        return false;
    }
}

double toMm(const VtdbToolGeometry& geometry, double value) {
    return geometry.units == VtdbUnits::Imperial ? value * 25.4 : value;
}

std::string lowerCopy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool containsCI(const std::string& text, const char* needle) {
    return lowerCopy(text).find(needle) != std::string::npos;
}

std::string stripVectricTokens(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    bool inToken = false;
    for (char c : text) {
        if (c == '{') {
            inToken = true;
            continue;
        }
        if (c == '}') {
            inToken = false;
            result.push_back(' ');
            continue;
        }
        if (!inToken)
            result.push_back(c);
    }
    return result;
}

std::string profileDescriptionText(const VtdbToolGeometry& geometry) {
    std::string text = stripVectricTokens(geometry.name_format);
    if (!geometry.notes.empty()) {
        text.push_back(' ');
        text += stripVectricTokens(geometry.notes);
    }
    if (!geometry.custom_attributes.empty()) {
        text.push_back(' ');
        text += geometry.custom_attributes;
    }
    return text;
}

std::string compactNumberToken(std::string token) {
    token.erase(std::remove_if(token.begin(), token.end(), [](unsigned char c) {
        return std::isspace(c);
    }), token.end());
    return token;
}

std::optional<double> parseNumberToken(const std::string& rawToken) {
    std::string token = compactNumberToken(rawToken);
    if (token.empty())
        return std::nullopt;

    const std::size_t mixed = token.find('-');
    if (mixed != std::string::npos) {
        auto whole = parseNumberToken(token.substr(0, mixed));
        auto frac = parseNumberToken(token.substr(mixed + 1));
        if (whole && frac)
            return *whole + *frac;
        return std::nullopt;
    }

    const std::size_t slash = token.find('/');
    if (slash != std::string::npos) {
        auto numerator = parseNumberToken(token.substr(0, slash));
        auto denominator = parseNumberToken(token.substr(slash + 1));
        if (numerator && denominator && *denominator != 0.0)
            return *numerator / *denominator;
        return std::nullopt;
    }

    try {
        return std::stod(token);
    } catch (...) {
        return std::nullopt;
    }
}

double measurementToMm(double value, std::string unit) {
    unit = lowerCopy(std::move(unit));
    if (unit == "mm" || unit == "millimeter" || unit == "millimeters" ||
        unit == "millimetre" || unit == "millimetres") {
        return value;
    }
    if (unit == "cm")
        return value * 10.0;
    return value * 25.4;
}

std::optional<double> findMeasurementMm(const std::string& text,
                                        const std::string& trailingPattern) {
    const std::regex pattern(
        std::string(kMeasurementPattern) + kUnitPattern + trailingPattern,
        std::regex_constants::icase);
    std::smatch match;
    if (!std::regex_search(text, match, pattern) || match.size() < 3)
        return std::nullopt;

    auto value = parseNumberToken(match[1].str());
    if (!value)
        return std::nullopt;
    return measurementToMm(*value, match[2].str());
}

std::optional<double> findCuttingLengthMm(const std::string& text) {
    return findMeasurementMm(text,
        R"(\s*(?:x\s*)?(?:cutting\s+length|length\s+of\s+cut|loc\b|cel\b))");
}

std::optional<double> findDiameterMm(const std::string& text) {
    return findMeasurementMm(text,
        R"(\s*(?:cutting\s+)?(?:dia\b|diameter|diamter))");
}

std::optional<double> findFlatDiameterMm(const std::string& text) {
    return findMeasurementMm(text, R"(\s*tip\s+width)");
}

std::optional<double> findTipRadiusMm(const std::string& text) {
    return findMeasurementMm(text, R"(\s*(?:radius|rad\b))");
}

std::optional<double> findTaperedSideAngleDeg(const std::string& text) {
    const std::regex explicitPattern(
        R"((\d+(?:\.\d+)?)\s*(?:deg|degree|degrees)\s*(?:tapered\s+angle|taper(?:ed)?))",
        std::regex_constants::icase);
    std::smatch match;
    if (std::regex_search(text, match, explicitPattern) && match.size() > 1) {
        auto value = parseNumberToken(match[1].str());
        if (value)
            return value;
    }

    if (!containsCI(text, "taper") || !containsCI(text, "ball"))
        return std::nullopt;

    const std::regex fallbackPattern(R"((\d+(?:\.\d+)?)\s*(?:deg|degree|degrees))",
                                     std::regex_constants::icase);
    if (std::regex_search(text, match, fallbackPattern) && match.size() > 1)
        return parseNumberToken(match[1].str());
    return std::nullopt;
}

bool nearlyEqual(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}

double fallbackCutHeightMm(ToolProfileShape shape,
                           double diameterMm,
                           double flatDiameterMm,
                           double tipRadiusMm,
                           double includedAngleDeg,
                           double sideAngleDeg) {
    if (shape == ToolProfileShape::TaperedBallNose &&
        diameterMm > 0.0 && tipRadiusMm > 0.0 && sideAngleDeg > 0.0) {
        const double sideAngleRad = sideAngleDeg * M_PI / 180.0;
        const double taperHeight = ((diameterMm * 0.5) - tipRadiusMm) /
            std::tan(sideAngleRad);
        if (std::isfinite(taperHeight) && taperHeight > 0.0)
            return taperHeight;
    }

    if ((shape == ToolProfileShape::VGroove ||
         shape == ToolProfileShape::DrillPoint) &&
        diameterMm > 0.0 && includedAngleDeg > 0.0) {
        const double halfAngle = includedAngleDeg * 0.5 * M_PI / 180.0;
        const double taperHeight = ((diameterMm - flatDiameterMm) * 0.5) /
            std::tan(halfAngle);
        if (std::isfinite(taperHeight) && taperHeight > 0.0)
            return taperHeight;
    }

    if (shape == ToolProfileShape::Laser)
        return std::max(10.0, diameterMm);

    return std::max(10.0, std::max(0.1, diameterMm) * 3.0);
}

void addNeedsMapping(ToolProfileDescriptor& descriptor) {
    descriptor.needsMapping = true;
    descriptor.badges.push_back("Needs Mapping");
}

void addBadge(ToolProfileDescriptor& descriptor, const char* badge) {
    if (std::find(descriptor.badges.begin(), descriptor.badges.end(), badge) ==
        descriptor.badges.end()) {
        descriptor.badges.push_back(badge);
    }
}

void addMissingGeometryBadges(ToolProfileDescriptor& descriptor,
                              const VtdbToolGeometry& geometry) {
    if (descriptor.shape != ToolProfileShape::Laser && geometry.diameter <= 0.0) {
        addBadge(descriptor, "Missing Diameter");
    }

    if ((descriptor.shape == ToolProfileShape::BallNose ||
         descriptor.shape == ToolProfileShape::TaperedBallNose) &&
        geometry.tip_radius <= 0.0) {
        addBadge(descriptor, "Missing Tip Radius");
    }

    if ((descriptor.shape == ToolProfileShape::VGroove ||
         descriptor.shape == ToolProfileShape::TaperedBallNose ||
         descriptor.shape == ToolProfileShape::DrillPoint ||
         descriptor.shape == ToolProfileShape::DiamondDrag) &&
        geometry.included_angle <= 0.0) {
        addBadge(descriptor, "Missing Angle");
    }
}

} // namespace

double toolProfileDiameterMm(const VtdbToolGeometry& geometry) {
    return toMm(geometry, geometry.diameter);
}

double toolProfileFlatDiameterMm(const VtdbToolGeometry& geometry) {
    return toMm(geometry, geometry.flat_diameter);
}

double toolProfileTipRadiusMm(const VtdbToolGeometry& geometry) {
    return toMm(geometry, geometry.tip_radius);
}

double toolProfileFluteLengthMm(const VtdbToolGeometry& geometry) {
    return toMm(geometry, geometry.flute_length);
}

ToolProfileResolvedGeometry resolveToolProfileGeometry(const VtdbToolGeometry& geometry) {
    const ToolProfileDescriptor descriptor = describeToolProfile(geometry);
    const std::string text = profileDescriptionText(geometry);

    ToolProfileResolvedGeometry resolved;
    resolved.shape = descriptor.shape;
    resolved.diameterMm = toolProfileDiameterMm(geometry);
    resolved.flatDiameterMm = toolProfileFlatDiameterMm(geometry);
    resolved.tipRadiusMm = toolProfileTipRadiusMm(geometry);
    resolved.cutHeightMm = toolProfileFluteLengthMm(geometry);
    resolved.includedAngleDeg = geometry.included_angle;

    if (resolved.diameterMm <= 0.0) {
        if (auto parsed = findDiameterMm(text)) {
            resolved.diameterMm = *parsed;
            resolved.diameterParsed = true;
        }
    }

    if (resolved.flatDiameterMm <= 0.0) {
        if (auto parsed = findFlatDiameterMm(text)) {
            resolved.flatDiameterMm = *parsed;
            resolved.flatDiameterParsed = true;
        }
    }

    if (resolved.tipRadiusMm <= 0.0) {
        if (auto parsed = findTipRadiusMm(text)) {
            resolved.tipRadiusMm = *parsed;
            resolved.tipRadiusParsed = true;
        }
    }

    if (resolved.shape == ToolProfileShape::TaperedBallNose) {
        const auto parsedSideAngle = findTaperedSideAngleDeg(text);
        if (parsedSideAngle &&
            (resolved.includedAngleDeg <= 0.0 ||
             nearlyEqual(resolved.includedAngleDeg, *parsedSideAngle * 2.0, 0.02))) {
            resolved.sideAngleDeg = *parsedSideAngle;
            resolved.sideAngleParsed = true;
        } else {
            resolved.sideAngleDeg = resolved.includedAngleDeg;
        }
    }

    if (resolved.cutHeightMm <= 0.0) {
        if (auto parsed = findCuttingLengthMm(text)) {
            resolved.cutHeightMm = *parsed;
            resolved.cutHeightParsed = true;
        }
    }

    if (resolved.diameterMm <= 0.0 && resolved.shape != ToolProfileShape::Laser)
        resolved.diameterMm = kDefaultPreviewDiameterMm;

    if (resolved.shape == ToolProfileShape::BallNose && resolved.tipRadiusMm <= 0.0 &&
        resolved.diameterMm > 0.0) {
        resolved.tipRadiusMm = resolved.diameterMm * 0.5;
    } else if (resolved.shape == ToolProfileShape::TaperedBallNose &&
               resolved.tipRadiusMm <= 0.0 && resolved.diameterMm > 0.0) {
        resolved.tipRadiusMm = resolved.diameterMm * 0.125;
    } else if (resolved.shape == ToolProfileShape::Radiused &&
               resolved.tipRadiusMm <= 0.0 && resolved.diameterMm > 0.0) {
        resolved.tipRadiusMm = resolved.diameterMm * 0.1;
    }

    if (resolved.cutHeightMm <= 0.0) {
        resolved.cutHeightMm = fallbackCutHeightMm(resolved.shape,
                                                   resolved.diameterMm,
                                                   resolved.flatDiameterMm,
                                                   resolved.tipRadiusMm,
                                                   resolved.includedAngleDeg,
                                                   resolved.sideAngleDeg);
    }

    resolved.topDiameterMm = resolved.diameterMm;
    if (resolved.shape == ToolProfileShape::TaperedBallNose &&
        resolved.tipRadiusMm > 0.0 && resolved.cutHeightMm > 0.0 &&
        resolved.sideAngleDeg > 0.0) {
        const double sideAngleRad = resolved.sideAngleDeg * M_PI / 180.0;
        const double taperedTop = (resolved.tipRadiusMm +
            std::tan(sideAngleRad) * resolved.cutHeightMm) * 2.0;
        if (std::isfinite(taperedTop) && taperedTop > 0.0) {
            resolved.topDiameterMm = resolved.diameterMm > 0.0
                ? std::min(resolved.diameterMm, taperedTop)
                : taperedTop;
        }
    }

    return resolved;
}

double toolProfilePreviewDiameterMm(const VtdbToolGeometry& geometry,
                                    const ToolProfileDescriptor& /*descriptor*/) {
    return resolveToolProfileGeometry(geometry).diameterMm;
}

double toolProfilePreviewFlatDiameterMm(const VtdbToolGeometry& geometry,
                                        const ToolProfileDescriptor& /*descriptor*/) {
    return std::max(0.0, resolveToolProfileGeometry(geometry).flatDiameterMm);
}

double toolProfilePreviewTipRadiusMm(const VtdbToolGeometry& geometry,
                                     const ToolProfileDescriptor& /*descriptor*/) {
    return resolveToolProfileGeometry(geometry).tipRadiusMm;
}

double toolProfilePreviewCutHeightMm(const VtdbToolGeometry& geometry,
                                     const ToolProfileDescriptor& /*descriptor*/) {
    return resolveToolProfileGeometry(geometry).cutHeightMm;
}

ToolProfileDescriptor describeToolProfile(const VtdbToolGeometry& geometry) {
    ToolProfileDescriptor descriptor;
    descriptor.rawToolType = static_cast<int>(geometry.tool_type);
    descriptor.typeKnown = isKnownToolType(geometry.tool_type);
    descriptor.needsMapping = !descriptor.typeKnown;

    const bool hasAngle = geometry.included_angle > 0.0;
    const bool hasLaser = geometry.laser_watt > 0 || descriptor.rawToolType == 12;

    auto finish = [&]() {
        addMissingGeometryBadges(descriptor, geometry);
        return descriptor;
    };

    if (hasLaser) {
        descriptor.shape = ToolProfileShape::Laser;
        descriptor.label = "Laser";
        if (!descriptor.typeKnown)
            descriptor.badges.push_back("Needs Mapping");
        return finish();
    }

    switch (geometry.tool_type) {
    case VtdbToolType::BallNose:
        descriptor.shape = ToolProfileShape::BallNose;
        descriptor.label = "Ball Nose";
        return finish();

    case VtdbToolType::EndMill:
        descriptor.shape = ToolProfileShape::FlatEnd;
        descriptor.label = toolProfileDiameterMm(geometry) >= 25.4
            ? "Surfacing / Spoilboard Cutter"
            : "End Mill";
        return finish();

    case VtdbToolType::Radiused:
        descriptor.shape = ToolProfileShape::Radiused;
        descriptor.label = "Radiused";
        return finish();

    case VtdbToolType::VBit:
        descriptor.shape = ToolProfileShape::VGroove;
        descriptor.label = "V-Bit";
        return finish();

    case VtdbToolType::TaperedBallNose:
        descriptor.shape = ToolProfileShape::TaperedBallNose;
        descriptor.label = "Tapered Ball Nose";
        return finish();

    case VtdbToolType::Drill:
        descriptor.shape = ToolProfileShape::DrillPoint;
        descriptor.label = "Drill";
        return finish();

    case VtdbToolType::ThreadMill:
        descriptor.shape = ToolProfileShape::FlatEnd;
        descriptor.label = "Thread Mill";
        return finish();

    case VtdbToolType::FormTool:
        descriptor.shape = geometry.outline.empty()
            ? ToolProfileShape::UnknownEnvelope
            : ToolProfileShape::FormOutline;
        descriptor.label = "Form Tool";
        return finish();

    case VtdbToolType::DiamondDrag:
        descriptor.shape = ToolProfileShape::DiamondDrag;
        descriptor.label = "Diamond Drag";
        return finish();

    default:
        break;
    }

    if (hasAngle) {
        descriptor.shape = ToolProfileShape::VGroove;
        descriptor.label = "V-Groove / Engraving";
        addNeedsMapping(descriptor);
        return finish();
    }

    if (geometry.tip_radius > 0.0 && geometry.diameter > 0.0) {
        descriptor.shape = ToolProfileShape::BallNose;
        descriptor.label = "Ball Nose";
        addNeedsMapping(descriptor);
        return finish();
    }

    if (geometry.diameter > 0.0) {
        descriptor.shape = ToolProfileShape::FlatEnd;
        descriptor.label = "Flat End";
        addNeedsMapping(descriptor);
        return finish();
    }

    descriptor.shape = ToolProfileShape::UnknownEnvelope;
    descriptor.label = "Unknown Tool";
    addNeedsMapping(descriptor);
    return finish();
}

} // namespace dw

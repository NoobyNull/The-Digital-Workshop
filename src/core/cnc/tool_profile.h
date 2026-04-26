#pragma once

#include <string>
#include <vector>

#include "cnc_tool.h"

namespace dw {

enum class ToolProfileShape {
    FlatEnd,
    BallNose,
    TaperedBallNose,
    VGroove,
    Radiused,
    DrillPoint,
    FormOutline,
    DiamondDrag,
    Laser,
    UnknownEnvelope
};

struct ToolProfileDescriptor {
    ToolProfileShape shape = ToolProfileShape::UnknownEnvelope;
    int rawToolType = 0;
    bool typeKnown = false;
    bool needsMapping = false;
    std::string label;
    std::vector<std::string> badges;
};

struct ToolProfileResolvedGeometry {
    ToolProfileShape shape = ToolProfileShape::UnknownEnvelope;
    double diameterMm = 0.0;
    double topDiameterMm = 0.0;
    double flatDiameterMm = 0.0;
    double tipRadiusMm = 0.0;
    double cutHeightMm = 0.0;
    double includedAngleDeg = 0.0;
    double sideAngleDeg = 0.0;
    bool diameterParsed = false;
    bool flatDiameterParsed = false;
    bool tipRadiusParsed = false;
    bool cutHeightParsed = false;
    bool sideAngleParsed = false;
};

ToolProfileDescriptor describeToolProfile(const VtdbToolGeometry& geometry);
ToolProfileResolvedGeometry resolveToolProfileGeometry(const VtdbToolGeometry& geometry);

double toolProfileDiameterMm(const VtdbToolGeometry& geometry);
double toolProfileFlatDiameterMm(const VtdbToolGeometry& geometry);
double toolProfileTipRadiusMm(const VtdbToolGeometry& geometry);
double toolProfileFluteLengthMm(const VtdbToolGeometry& geometry);

double toolProfilePreviewDiameterMm(const VtdbToolGeometry& geometry,
                                    const ToolProfileDescriptor& descriptor);
double toolProfilePreviewFlatDiameterMm(const VtdbToolGeometry& geometry,
                                        const ToolProfileDescriptor& descriptor);
double toolProfilePreviewTipRadiusMm(const VtdbToolGeometry& geometry,
                                     const ToolProfileDescriptor& descriptor);
double toolProfilePreviewCutHeightMm(const VtdbToolGeometry& geometry,
                                     const ToolProfileDescriptor& descriptor);

} // namespace dw

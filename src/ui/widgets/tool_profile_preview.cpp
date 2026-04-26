#include "tool_profile_preview.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/cnc/tool_profile.h"

namespace dw {

namespace {

constexpr float kMinCanvasHeight = 150.0f;
constexpr float kPi = 3.14159265358979323846f;

ImU32 colorU32(float r, float g, float b, float a = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}

ImU32 profileColor(ToolProfileShape shape) {
    switch (shape) {
    case ToolProfileShape::VGroove: return colorU32(0.93f, 0.72f, 0.28f);
    case ToolProfileShape::BallNose:
    case ToolProfileShape::TaperedBallNose: return colorU32(0.41f, 0.72f, 0.94f);
    case ToolProfileShape::FlatEnd:
    case ToolProfileShape::Radiused: return colorU32(0.46f, 0.81f, 0.58f);
    case ToolProfileShape::DrillPoint: return colorU32(0.86f, 0.60f, 0.42f);
    case ToolProfileShape::Laser: return colorU32(0.98f, 0.35f, 0.31f);
    default: return colorU32(0.62f, 0.68f, 0.72f);
    }
}

std::string formatMm(double value) {
    char buf[64];
    if (value >= 10.0)
        std::snprintf(buf, sizeof(buf), "%.1f mm", value);
    else
        std::snprintf(buf, sizeof(buf), "%.2f mm", value);
    return buf;
}

void drawCenterline(ImDrawList* draw, const ImVec2& top, const ImVec2& bottom, ImU32 color) {
    const float dash = 5.0f;
    for (float y = top.y; y < bottom.y; y += dash * 2.0f) {
        draw->AddLine(ImVec2(top.x, y),
                      ImVec2(top.x, std::min(y + dash, bottom.y)),
                      color, 1.0f);
    }
}

void fillPolyline(ImDrawList* draw,
                  const std::vector<ImVec2>& points,
                  ImU32 fill,
                  ImU32 stroke) {
    if (points.size() < 3)
        return;
    draw->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), fill);
    draw->AddPolyline(points.data(), static_cast<int>(points.size()), stroke,
                      ImDrawFlags_Closed, 1.5f);
}

struct ProfileFrame {
    ImVec2 min;
    ImVec2 max;
    ImVec2 center;
    float widthPx = 0.0f;
    float heightPx = 0.0f;
    double diameterMm = 1.0;
    double heightMm = 1.0;
    double pxPerMm = 1.0;
};

ProfileFrame makeFrame(const ImVec2& min, const ImVec2& max,
                       const VtdbToolGeometry& geometry,
                       const ToolProfileDescriptor& profile) {
    ProfileFrame f;
    f.min = min;
    f.max = max;
    f.center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    f.widthPx = max.x - min.x;
    f.heightPx = max.y - min.y;
    const ToolProfileResolvedGeometry resolved = resolveToolProfileGeometry(geometry);
    f.diameterMm = std::max({0.1, resolved.diameterMm, resolved.topDiameterMm,
                             resolved.flatDiameterMm, resolved.tipRadiusMm * 2.0});
    f.heightMm = std::max(0.1, toolProfilePreviewCutHeightMm(geometry, profile));
    const double widthScale = static_cast<double>(f.widthPx * 0.58f) / f.diameterMm;
    const double heightScale = static_cast<double>(f.heightPx * 0.74f) / f.heightMm;
    f.pxPerMm = std::max(0.01, std::min(widthScale, heightScale));
    return f;
}

float widthToPx(const ProfileFrame& f, double widthMm) {
    return static_cast<float>(widthMm * f.pxPerMm);
}

float heightToPx(const ProfileFrame& f, double heightMm) {
    return static_cast<float>(heightMm * f.pxPerMm);
}

void drawFlatEnd(ImDrawList* draw, const ProfileFrame& f,
                 const VtdbToolGeometry& geometry, ImU32 fill, ImU32 stroke) {
    const auto resolved = resolveToolProfileGeometry(geometry);
    const float halfW = widthToPx(f, resolved.diameterMm) * 0.5f;
    const float h = heightToPx(f, f.heightMm);
    const float y0 = f.center.y - h * 0.5f;
    const float y1 = f.center.y + h * 0.5f;
    fillPolyline(draw, {
        ImVec2(f.center.x - halfW, y0),
        ImVec2(f.center.x + halfW, y0),
        ImVec2(f.center.x + halfW, y1),
        ImVec2(f.center.x - halfW, y1),
    }, fill, stroke);
}

void drawVGroove(ImDrawList* draw, const ProfileFrame& f,
                 const VtdbToolGeometry& geometry, ImU32 fill, ImU32 stroke) {
    const auto resolved = resolveToolProfileGeometry(geometry);
    const float topHalfW = widthToPx(f, resolved.diameterMm) * 0.5f;
    const float flatHalfW = widthToPx(f, resolved.flatDiameterMm) * 0.5f;
    const float h = heightToPx(f, f.heightMm);
    const float y0 = f.center.y - h * 0.5f;
    const float y1 = f.center.y + h * 0.5f;

    if (flatHalfW > 1.0f) {
        fillPolyline(draw, {
            ImVec2(f.center.x - topHalfW, y0),
            ImVec2(f.center.x + topHalfW, y0),
            ImVec2(f.center.x + flatHalfW, y1),
            ImVec2(f.center.x - flatHalfW, y1),
        }, fill, stroke);
    } else {
        fillPolyline(draw, {
            ImVec2(f.center.x - topHalfW, y0),
            ImVec2(f.center.x + topHalfW, y0),
            ImVec2(f.center.x, y1),
        }, fill, stroke);
    }
}

void drawBallNose(ImDrawList* draw, const ProfileFrame& f,
                  const VtdbToolGeometry& geometry,
                  bool tapered, ImU32 fill, ImU32 stroke) {
    const auto resolved = resolveToolProfileGeometry(geometry);
    const double diameterMm = tapered ? resolved.topDiameterMm : resolved.diameterMm;
    const double tipRadiusMm = resolved.tipRadiusMm;
    const float topHalfW = widthToPx(f, diameterMm) * 0.5f;
    const float tipHalfW = widthToPx(f, tipRadiusMm * 2.0) * 0.5f;
    const float h = heightToPx(f, f.heightMm);
    const float y0 = f.center.y - h * 0.5f;
    const float arcCenterY = f.center.y + h * 0.5f - tipHalfW;
    const float shoulderY = arcCenterY;
    const float shoulderHalfW = tapered ? tipHalfW : topHalfW;

    std::vector<ImVec2> points;
    points.push_back(ImVec2(f.center.x - topHalfW, y0));
    points.push_back(ImVec2(f.center.x + topHalfW, y0));
    points.push_back(ImVec2(f.center.x + shoulderHalfW, shoulderY));
    for (int i = 0; i <= 16; ++i) {
        const float t = static_cast<float>(i) / 16.0f;
        const float angle = t * kPi;
        points.push_back(ImVec2(f.center.x + std::cos(angle) * tipHalfW,
                                arcCenterY + std::sin(angle) * tipHalfW));
    }
    points.push_back(ImVec2(f.center.x - shoulderHalfW, shoulderY));
    fillPolyline(draw, points, fill, stroke);
}

void drawDrill(ImDrawList* draw, const ProfileFrame& f,
               const VtdbToolGeometry& geometry, ImU32 fill, ImU32 stroke) {
    const auto resolved = resolveToolProfileGeometry(geometry);
    const float halfW = widthToPx(f, resolved.diameterMm) * 0.5f;
    const float h = heightToPx(f, f.heightMm);
    const float y0 = f.center.y - h * 0.5f;
    const float yPoint = f.center.y + h * 0.5f;
    const float yShoulder = yPoint - std::min(h * 0.35f, halfW * 1.2f);
    fillPolyline(draw, {
        ImVec2(f.center.x - halfW, y0),
        ImVec2(f.center.x + halfW, y0),
        ImVec2(f.center.x + halfW, yShoulder),
        ImVec2(f.center.x, yPoint),
        ImVec2(f.center.x - halfW, yShoulder),
    }, fill, stroke);
}

void drawLaser(ImDrawList* draw, const ProfileFrame& f,
               const VtdbToolGeometry& geometry, ImU32 stroke) {
    const float h = f.heightPx * 0.72f;
    const float y0 = f.center.y - h * 0.5f;
    const float y1 = f.center.y + h * 0.5f;
    const float kerf = geometry.laser_watt > 0 ? 16.0f : 9.0f;
    draw->AddLine(ImVec2(f.center.x, y0), ImVec2(f.center.x, y1), stroke, 3.0f);
    draw->AddLine(ImVec2(f.center.x - kerf, y1), ImVec2(f.center.x + kerf, y1), stroke, 2.0f);
    draw->AddTriangle(ImVec2(f.center.x, y0),
                      ImVec2(f.center.x - kerf * 0.5f, y1),
                      ImVec2(f.center.x + kerf * 0.5f, y1),
                      colorU32(0.98f, 0.35f, 0.31f, 0.15f), 1.0f);
}

void drawUnknown(ImDrawList* draw, const ProfileFrame& f, ImU32 stroke) {
    const ImVec2 boxMin(f.center.x - f.widthPx * 0.16f, f.center.y - f.heightPx * 0.28f);
    const ImVec2 boxMax(f.center.x + f.widthPx * 0.16f, f.center.y + f.heightPx * 0.28f);
    draw->AddRect(boxMin, boxMax, stroke, 5.0f, 0, 1.5f);
    draw->AddText(ImVec2(f.center.x - 4.0f, f.center.y - 8.0f), stroke, "?");
}

} // namespace

void renderToolProfilePreview(const VtdbToolGeometry& geometry, ImVec2 size) {
    const ToolProfileDescriptor profile = describeToolProfile(geometry);
    const ToolProfileResolvedGeometry resolved = resolveToolProfileGeometry(geometry);

    if (size.x <= 0.0f)
        size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0.0f)
        size.y = 210.0f;
    size.y = std::max(size.y, kMinCanvasHeight);

    ImGui::SeparatorText("Cut Profile");
    if (!ImGui::BeginChild("##toolProfilePreview", size, ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return;
    }

    ImGui::TextUnformatted(profile.label.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("raw type %d", profile.rawToolType);
    for (const auto& badge : profile.badges) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.98f, 0.67f, 0.25f, 1.0f), "%s", badge.c_str());
    }
    if (resolved.diameterParsed || resolved.flatDiameterParsed ||
        resolved.tipRadiusParsed || resolved.cutHeightParsed ||
        resolved.sideAngleParsed) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.47f, 0.79f, 0.96f, 1.0f), "Parsed Description");
    }

    std::string dimLine = resolved.diameterMm > 0.0
        ? "Diameter " + formatMm(resolved.diameterMm)
        : "Diameter missing";
    if (profile.shape == ToolProfileShape::TaperedBallNose && resolved.sideAngleDeg > 0.0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), " | Side %.2f deg", resolved.sideAngleDeg);
        dimLine += buf;
    } else if (resolved.includedAngleDeg > 0.0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), " | Angle %.1f deg", resolved.includedAngleDeg);
        dimLine += buf;
    }
    if (resolved.flatDiameterMm > 0.0)
        dimLine += " | Flat " + formatMm(resolved.flatDiameterMm);
    if (resolved.tipRadiusMm > 0.0)
        dimLine += " | Tip R " + formatMm(resolved.tipRadiusMm);
    if (resolved.cutHeightMm > 0.0)
        dimLine += " | Cut length " + formatMm(resolved.cutHeightMm);
    ImGui::TextDisabled("%s", dimLine.c_str());

    const ImVec2 canvasSize(ImGui::GetContentRegionAvail().x,
                            std::max(96.0f, ImGui::GetContentRegionAvail().y));
    ImGui::InvisibleButton("##toolProfileCanvas", canvasSize);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetItemRectMin();
    const ImVec2 p1 = ImGui::GetItemRectMax();
    draw->AddRectFilled(p0, p1, colorU32(0.07f, 0.10f, 0.12f), 5.0f);
    draw->AddRect(p0, p1, colorU32(0.22f, 0.31f, 0.36f), 5.0f);

    const ImVec2 innerMin(p0.x + 14.0f, p0.y + 10.0f);
    const ImVec2 innerMax(p1.x - 14.0f, p1.y - 12.0f);
    const ProfileFrame frame = makeFrame(innerMin, innerMax, geometry, profile);
    drawCenterline(draw,
                   ImVec2(frame.center.x, innerMin.y),
                   ImVec2(frame.center.x, innerMax.y),
                   colorU32(0.42f, 0.50f, 0.55f, 0.55f));

    const ImU32 fill = profileColor(profile.shape);
    const ImU32 stroke = colorU32(0.93f, 0.97f, 0.98f);
    const ImU32 fillAlpha = (fill & 0x00ffffffu) | (0xb0000000u);

    switch (profile.shape) {
    case ToolProfileShape::FlatEnd:
    case ToolProfileShape::Radiused:
    case ToolProfileShape::FormOutline:
        drawFlatEnd(draw, frame, geometry, fillAlpha, stroke);
        break;
    case ToolProfileShape::VGroove:
    case ToolProfileShape::DiamondDrag:
        drawVGroove(draw, frame, geometry, fillAlpha, stroke);
        break;
    case ToolProfileShape::BallNose:
        drawBallNose(draw, frame, geometry, false, fillAlpha, stroke);
        break;
    case ToolProfileShape::TaperedBallNose:
        drawBallNose(draw, frame, geometry, true, fillAlpha, stroke);
        break;
    case ToolProfileShape::DrillPoint:
        drawDrill(draw, frame, geometry, fillAlpha, stroke);
        break;
    case ToolProfileShape::Laser:
        drawLaser(draw, frame, geometry, stroke);
        break;
    case ToolProfileShape::UnknownEnvelope:
        drawUnknown(draw, frame, stroke);
        break;
    }

    if (profile.shape == ToolProfileShape::FormOutline && !geometry.outline.empty()) {
        draw->AddText(ImVec2(innerMin.x, innerMax.y - ImGui::GetTextLineHeight()),
                      colorU32(0.70f, 0.79f, 0.84f),
                      "Stored outline present; rendered as envelope for now");
    } else if (profile.shape == ToolProfileShape::UnknownEnvelope) {
        draw->AddText(ImVec2(innerMin.x, innerMax.y - ImGui::GetTextLineHeight()),
                      colorU32(0.98f, 0.67f, 0.25f),
                      "Not enough geometry to infer cut profile");
    }

    ImGui::EndChild();
}

} // namespace dw

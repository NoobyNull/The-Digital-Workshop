#include <gtest/gtest.h>

#include "core/carve/carve_job.h"
#include "core/carve/toolpath_preview.h"

#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>

using namespace dw;
using namespace dw::carve;

namespace {

Toolpath makeDenseRaster(std::size_t lineCount, std::size_t pointsPerLine)
{
    Toolpath path;
    path.points.reserve(lineCount * (pointsPerLine + 2));

    for (std::size_t line = 0; line < lineCount; ++line) {
        const f32 y = static_cast<f32>(line);
        const bool forward = line % 2 == 0;
        const f32 startX = forward ? 0.0f : 100.0f;

        if (!path.points.empty()) {
            Vec3 retract = path.points.back().position;
            retract.z = 5.0f;
            path.points.push_back({retract, true});
        }
        path.points.push_back({Vec3{startX, y, 5.0f}, true});

        for (std::size_t point = 0; point < pointsPerLine; ++point) {
            const f32 progress = pointsPerLine == 1
                                     ? 0.0f
                                     : static_cast<f32>(point) /
                                           static_cast<f32>(pointsPerLine - 1);
            const f32 x = forward ? progress * 100.0f
                                  : (1.0f - progress) * 100.0f;
            path.points.push_back({Vec3{x, y, -1.0f}, false});
        }
    }
    path.scanLineCount = static_cast<int>(lineCount);
    path.lineCount = static_cast<int>(path.points.size());
    return path;
}

std::size_t previewSegmentCount(const ToolpathPreviewGeometry& geometry,
                                const ToolpathPreviewSelection& selection)
{
    std::size_t count = 0;
    for (const std::size_t index : selection.strokeIndices) {
        const auto& stroke = geometry.strokes[index];
        if (stroke.rapidPoints.size() > 1) count += stroke.rapidPoints.size() - 1;
        if (stroke.cutPoints.size() > 1) count += stroke.cutPoints.size() - 1;
    }
    return count;
}

} // namespace

TEST(ToolpathPreview, DenseRasterCollapsesInterpolationIntoCuttingStrokes)
{
    const Toolpath source = makeDenseRaster(735, 338);
    ASSERT_GT(source.points.size(), 249000U);

    const ToolpathPreviewGeometry preview = buildToolpathPreviewGeometry(source);

    EXPECT_EQ(preview.sourceMoveCount, source.points.size() - 1);
    ASSERT_EQ(preview.strokes.size(), 735U);
    for (const auto& stroke : preview.strokes) {
        EXPECT_EQ(stroke.cutPoints.size(), 2U);
        EXPECT_LE(stroke.rapidPoints.size(), 2U);
    }
}

TEST(ToolpathPreview, DenseRasterSelectsReadableScreenSpacedStrokes)
{
    const ToolpathPreviewGeometry preview =
        buildToolpathPreviewGeometry(makeDenseRaster(735, 338));
    const f32 fitScale = 356.0f / 734.0f;

    const auto fitted = selectToolpathPreviewStrokes(
        preview, fitScale, fitScale, 3.0f, 2000);
    const auto zoomed = selectToolpathPreviewStrokes(
        preview, fitScale * 4.0f, fitScale * 4.0f, 3.0f, 2000);

    EXPECT_GT(fitted.strokeIndices.size(), 100U);
    EXPECT_LT(fitted.strokeIndices.size(), 150U);
    EXPECT_LT(fitted.strokeIndices.size(), zoomed.strokeIndices.size());
    EXPECT_LE(zoomed.strokeIndices.size(), preview.strokes.size());
    EXPECT_LT(previewSegmentCount(preview, fitted), 300U);
    EXPECT_TRUE(fitted.simplified());
}

TEST(ToolpathPreview, SameScreenCellKeepsDifferentCutOrientations)
{
    ToolpathPreviewGeometry preview;
    preview.strokes = {
        {{}, {{-5.0f, 0.0f, -1.0f}, {5.0f, 0.0f, -1.0f}}},
        {{}, {{0.0f, -5.0f, -1.0f}, {0.0f, 5.0f, -1.0f}}},
    };

    const auto selected =
        selectToolpathPreviewStrokes(preview, 1.0f, 1.0f, 20.0f, 2000);

    EXPECT_EQ(selected.strokeIndices.size(), 2U);
}

TEST(ToolpathPreview, RepeatedDepthPassesDoNotOverdrawTheSameTopViewStroke)
{
    ToolpathPreviewGeometry preview;
    for (int depth = 1; depth <= 5; ++depth) {
        preview.strokes.push_back(
            {{}, {{0.0f, 10.0f, -static_cast<f32>(depth)},
                  {100.0f, 10.0f, -static_cast<f32>(depth)}}});
    }

    const auto selected =
        selectToolpathPreviewStrokes(preview, 1.0f, 1.0f, 3.0f, 2000);

    ASSERT_EQ(selected.strokeIndices.size(), 1U);
    EXPECT_TRUE(selected.simplified());
}

TEST(ToolpathPreview, CarveJobCachesPreviewWhenGeneratingFixedDepthPath)
{
    CarveJob job;
    ToolpathConfig config;
    config.axis = ScanAxis::XOnly;
    config.direction = MillDirection::Alternating;
    config.customStepoverPct = 100.0f;
    config.scanResolutionMm = 1.0f;
    config.stepdownMm = 10.0f;

    VtdbToolGeometry tool;
    tool.tool_type = VtdbToolType::EndMill;
    tool.units = VtdbUnits::Metric;
    tool.diameter = 2.0;

    job.generateFixedDepthToolpath(Vec3{0.0f, 0.0f, 0.0f},
                                   Vec3{20.0f, 10.0f, 0.0f},
                                   Vec3{0.0f, 0.0f, 0.0f},
                                   Vec3{20.0f, 10.0f, 0.0f},
                                   1.0f,
                                   config,
                                   tool);

    ASSERT_EQ(job.state(), CarveJobState::Ready);
    EXPECT_EQ(job.toolpathPreview().finishing.strokes.size(),
              static_cast<std::size_t>(job.toolpath().finishing.scanLineCount));
    EXPECT_LT(job.toolpathPreview().finishing.strokes.front().cutPoints.size(),
              job.toolpath().finishing.points.size());
}

TEST(ToolpathPreview, DirectCarveUiRendersCachedStrokesInsteadOfRawMoves)
{
    const std::string path =
        std::string(CMAKE_SOURCE_DIR) +
        "/src/ui/panels/direct_carve_carve_preview_step.cpp";
    std::ifstream input(path);
    ASSERT_TRUE(input.is_open());
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());

    EXPECT_NE(source.find("m_carveJob->toolpathPreview()"), std::string::npos);
    EXPECT_NE(source.find("selectToolpathPreviewStrokes"), std::string::npos);
    EXPECT_EQ(source.find("tp.finishing.points[i]"), std::string::npos);
    EXPECT_EQ(source.find("tp.clearing.points[i]"), std::string::npos);
}

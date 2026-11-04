#include "core/cam/cam_job_spec.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace dw::cam {

TEST(CamJobSpec, BuildsDefaultSurfacingSpecForMesh) {
    CamJobRequest request;
    request.modelName = "River Sign";
    request.meshPath = "/tmp/models/river-sign.stl";
    request.machineId = "grbl";

    const auto parsed = nlohmann::json::parse(buildDefaultSurfacingJobSpec(request));

    EXPECT_EQ(parsed.at("name"), "River Sign");
    EXPECT_EQ(parsed.at("machine"), "grbl");
    EXPECT_EQ(parsed.at("units"), "mm");
    EXPECT_EQ(parsed.at("stock"), "auto");

    ASSERT_EQ(parsed.at("features").size(), 1);
    EXPECT_EQ(parsed.at("features")[0].at("type"), "mesh");
    EXPECT_EQ(parsed.at("features")[0].at("path"), "/tmp/models/river-sign.stl");

    ASSERT_EQ(parsed.at("operations").size(), 2);
    EXPECT_EQ(parsed.at("operations")[0].at("kind"), "rough_surface");
    EXPECT_EQ(parsed.at("operations")[1].at("kind"), "finish_surface");
    ASSERT_EQ(parsed.at("tools").size(), 2);

    // Inline G-code return path: the spec must not ask the bridge to write files.
    EXPECT_FALSE(parsed.contains("outputPath"));
    EXPECT_FALSE(parsed.contains("saveProjectPath"));
}

TEST(CamJobSpec, EmitsAxisSwapForMeshFeature) {
    CamJobRequest request;
    request.modelName = "Odin";
    request.meshPath = "/tmp/odin.stl";
    request.axisSwap = "yz";

    const auto parsed = nlohmann::json::parse(buildDefaultSurfacingJobSpec(request));
    EXPECT_EQ(parsed.at("features")[0].at("axisSwap"), "yz");
}

TEST(CamJobSpec, LayFlatPicksSwapMinimizingHeight) {
    // Standing dagger: tall Z, shallow Y depth -> lay onto its back via yz.
    EXPECT_EQ(layFlatAxisSwap(80, 25, 400), "yz");
    // Long thin X-dominant part standing on end -> xz.
    EXPECT_EQ(layFlatAxisSwap(10, 200, 300), "xz");
    // Already flat relief -> unchanged.
    EXPECT_EQ(layFlatAxisSwap(300, 200, 40), "none");
    // Cube -> no pointless swap.
    EXPECT_EQ(layFlatAxisSwap(100, 100, 100), "none");
}

TEST(CamJobSpec, DefaultsToFluidncMachine) {
    CamJobRequest request;
    request.modelName = "Dome";
    request.meshPath = "/tmp/dome.stl";

    const auto parsed = nlohmann::json::parse(buildDefaultSurfacingJobSpec(request));
    EXPECT_EQ(parsed.at("machine"), "fluidnc");
}

} // namespace dw::cam

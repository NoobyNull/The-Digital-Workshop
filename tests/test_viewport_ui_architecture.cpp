#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readSource(const std::string& relativePath) {
    std::ifstream input(std::string(CMAKE_SOURCE_DIR) + "/" + relativePath);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

std::size_t lineCount(const std::string& text) {
    return static_cast<std::size_t>(
        std::count(text.begin(), text.end(), '\n'));
}

} // namespace

TEST(ViewportUiArchitecture, OwnedTranslationUnitsStayBelowSessionCeiling) {
    constexpr std::array<const char*, 5> files = {
        "src/ui/panels/viewport_panel.cpp",
        "src/ui/panels/viewport_toolbar.cpp",
        "src/ui/panels/viewport_interaction.cpp",
        "src/ui/panels/viewport_overlays.cpp",
        "src/ui/panels/viewport_gcode_layer.cpp",
    };

    for (const char* file : files) {
        const auto source = readSource(file);
        ASSERT_FALSE(source.empty()) << file;
        EXPECT_LE(lineCount(source), 650U) << file;
    }
}

TEST(ViewportUiArchitecture, ShellDelegatesToolbarInteractionOverlaysAndGcode) {
    const auto shell = readSource("src/ui/panels/viewport_panel.cpp");
    const auto toolbar = readSource("src/ui/panels/viewport_toolbar.cpp");
    const auto interaction = readSource("src/ui/panels/viewport_interaction.cpp");
    const auto overlays = readSource("src/ui/panels/viewport_overlays.cpp");
    const auto gcode = readSource("src/ui/panels/viewport_gcode_layer.cpp");

    EXPECT_EQ(shell.find("void ViewportPanel::renderToolbar()"), std::string::npos);
    EXPECT_EQ(shell.find("void ViewportPanel::handleInput()"), std::string::npos);
    EXPECT_EQ(shell.find("void ViewportPanel::renderViewport()"), std::string::npos);
    EXPECT_EQ(shell.find("void ViewportPanel::buildGCodeGeometry()"), std::string::npos);
    EXPECT_NE(toolbar.find("void ViewportPanel::renderToolbar()"), std::string::npos);
    EXPECT_NE(interaction.find("void ViewportPanel::handleInput()"), std::string::npos);
    EXPECT_NE(interaction.find("void ViewportPanel::renderViewCube()"), std::string::npos);
    EXPECT_NE(overlays.find("void ViewportPanel::renderViewport()"), std::string::npos);
    EXPECT_NE(overlays.find("void ViewportPanel::renderIdentityOverlay()"), std::string::npos);
    EXPECT_NE(gcode.find("void ViewportPanel::buildGCodeGeometry()"), std::string::npos);
    EXPECT_NE(gcode.find("void ViewportPanel::renderGCodeLines()"), std::string::npos);
}

TEST(ViewportUiArchitecture, NavigationAndDirtyPreviewFixesRemainOnRealPath) {
    const auto shell = readSource("src/ui/panels/viewport_panel.cpp");
    const auto toolbar = readSource("src/ui/panels/viewport_toolbar.cpp");
    const auto interaction = readSource("src/ui/panels/viewport_interaction.cpp");

    EXPECT_NE(shell.find("void ViewportPanel::setMesh"), std::string::npos);
    EXPECT_NE(shell.find("clearFitParams();"), std::string::npos);
    EXPECT_NE(shell.find("setTargetToBoundsCenter"), std::string::npos);
    EXPECT_NE(toolbar.find("nextNavStyle(navStyle)"), std::string::npos);
    EXPECT_NE(toolbar.find("tiltViewByQuarterTurns(1)"), std::string::npos);
    EXPECT_NE(toolbar.find("rotateViewByQuarterTurns(-1)"), std::string::npos);
    EXPECT_NE(interaction.find("centerCameraOnVisibleModel();"), std::string::npos);
    EXPECT_NE(interaction.find("m_modelMatrix * Vec4"), std::string::npos);
    EXPECT_NE(interaction.find("viewCubeFaceCanContainLabel(projectedFace"),
              std::string::npos);
}

TEST(ViewportUiArchitecture, PresentationContractHasNoProjectOrLibraryPolicyDependency) {
    const auto header =
        readSource("src/modules/viewport/viewport_presentation.h");
    const auto implementation =
        readSource("src/modules/viewport/viewport_presentation.cpp");
    const std::string combined = header + implementation;

    EXPECT_EQ(combined.find("ProjectSession"), std::string::npos);
    EXPECT_EQ(combined.find("project_repository"), std::string::npos);
    EXPECT_EQ(combined.find("LibraryPicker"), std::string::npos);
    EXPECT_EQ(combined.find("workshop_contract"), std::string::npos);
    EXPECT_EQ(combined.find("#include <imgui"), std::string::npos);
}

TEST(ViewportUiArchitecture, ApplicationSuppliesIdentityAtOwnedCommitBoundaries) {
    const auto callbacks = readSource("src/app/application_callbacks.cpp");
    const auto library = readSource("src/app/application_wiring_library.cpp");
    const auto resume = readSource("src/app/application_project_resume.cpp");
    const auto session = readSource("src/app/application_project_session.cpp");

    EXPECT_NE(callbacks.find("PresentationIdentity::libraryPreview"), std::string::npos);
    EXPECT_NE(library.find("PresentationIdentity::libraryPreview"), std::string::npos);
    EXPECT_NE(resume.find("PresentationIdentity::projectItem"), std::string::npos);
    EXPECT_NE(session.find("PresentationIdentity::none"), std::string::npos);
}

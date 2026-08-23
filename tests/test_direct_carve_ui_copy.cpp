#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readSource(const std::string& relativePath) {
    std::ifstream file(std::string(CMAKE_SOURCE_DIR) + "/" + relativePath);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string readDirectCarveSources() {
    std::string source = readSource("src/ui/panels/direct_carve_panel.cpp");
    for (const auto* step : {"direct_carve_design_size_step.cpp",
                             "direct_carve_material_blank_step.cpp",
                             "direct_carve_choose_tool_step.cpp",
                             "direct_carve_carve_preview_step.cpp"}) {
        source += readSource(std::string("src/ui/panels/") + step);
    }
    return source;
}

std::string readDirectCarvePanelHeader() {
    std::ifstream file(std::string(CMAKE_SOURCE_DIR) + "/src/ui/panels/direct_carve_panel.h");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string readDirectCarveOperationResume() {
    std::ifstream file(std::string(CMAKE_SOURCE_DIR) +
                       "/src/ui/panels/direct_carve_operation_resume.cpp");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string readApplicationCallbacks() {
    std::ifstream file(std::string(CMAKE_SOURCE_DIR) + "/src/app/application_callbacks.cpp");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string readViewportPanel() {
    std::ifstream file(std::string(CMAKE_SOURCE_DIR) + "/src/ui/panels/viewport_panel.cpp");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace

TEST(DirectCarveUiCopy, SeparatesMaterialBlankFromMachineTravel) {
    const std::string source = readDirectCarveSources();

    EXPECT_NE(source.find("Material Blank:"), std::string::npos);
    EXPECT_NE(source.find("Use Machine Travel"), std::string::npos);
    EXPECT_NE(source.find("Use Cut Part"), std::string::npos);
    EXPECT_NE(source.find("Fits blank"), std::string::npos);
    EXPECT_NE(source.find("Fits machine travel"), std::string::npos);
    EXPECT_NE(source.find("Blank usage"), std::string::npos);

    EXPECT_EQ(source.find("Stock Dimensions:"), std::string::npos);
    EXPECT_EQ(source.find("From Machine Profile"), std::string::npos);
    EXPECT_EQ(source.find("From Cut List"), std::string::npos);
    EXPECT_EQ(source.find("Fits stock"), std::string::npos);
    EXPECT_EQ(source.find("Fits machine\""), std::string::npos);
    EXPECT_EQ(source.find("Stock usage"), std::string::npos);
}

TEST(DirectCarveUiCopy, PlanningStepsExplainOrderSuggestionsAndAdvancedControls) {
    const auto design = readSource("src/ui/panels/direct_carve_design_size_step.cpp");
    const auto material = readSource("src/ui/panels/direct_carve_material_blank_step.cpp");
    const auto tool = readSource("src/ui/panels/direct_carve_choose_tool_step.cpp");
    const auto preview = readSource("src/ui/panels/direct_carve_carve_preview_step.cpp");

    EXPECT_NE(design.find("Design & Size"), std::string::npos);
    EXPECT_NE(material.find("Material & Blank"), std::string::npos);
    EXPECT_NE(material.find("before choosing a tool"), std::string::npos);
    EXPECT_NE(material.find("suggestions, not safety guarantees"), std::string::npos);

    const auto materialAdvanced = material.find("Advanced feed & toolpath settings");
    ASSERT_NE(materialAdvanced, std::string::npos);
    EXPECT_LT(materialAdvanced, material.find("Feed Rate (mm/min)"));
    EXPECT_LT(materialAdvanced, material.find("Path Detail (mm)"));
    EXPECT_LT(materialAdvanced, material.find("Scan Axis"));

    EXPECT_NE(tool.find("Choose Tool"), std::string::npos);
    EXPECT_NE(tool.find("Tool options are suggestions"), std::string::npos);
    EXPECT_NE(tool.find("Why this matters"), std::string::npos);

    EXPECT_NE(preview.find("Carve Preview"), std::string::npos);
    EXPECT_NE(preview.find("Suggested roughing tool"), std::string::npos);
    EXPECT_NE(preview.find("Why:"), std::string::npos);
    EXPECT_NE(preview.find("Advanced preview details"), std::string::npos);
    EXPECT_NE(preview.find("Save G-code"), std::string::npos);
}

TEST(DirectCarveUiCopy, FitPreviewDoesNotHijackOrdinaryModelLoad) {
    const std::string header = readDirectCarvePanelHeader();
    const std::string operationResume = readDirectCarveOperationResume();
    const std::string app = readApplicationCallbacks();
    const std::string viewport = readViewportPanel();

    EXPECT_NE(header.find("bool notifyFitPreview"), std::string::npos);
    EXPECT_NE(operationResume.find("notifyFitPreview && m_onFitParamsChanged"), std::string::npos);
    EXPECT_NE(app.find("m_uiManager->showDirectCarve()"), std::string::npos);
    EXPECT_NE(viewport.find("clearFitParams();"), std::string::npos);
}

TEST(DirectCarveUiCopy, DistinguishesLibraryMetadataFromProjectOperationMaterial) {
    const auto libraryActions = readSource("src/ui/panels/library_panel_actions.cpp");
    const auto materialsPanel = readSource("src/ui/panels/materials_panel.cpp");
    const auto projectMaterial =
        readSource("src/ui/panels/direct_carve_material_blank_step.cpp");
    const auto projectPersistence =
        readSource("src/ui/panels/direct_carve_project_sync.cpp");

    EXPECT_NE(libraryActions.find("Assign Library Default Material"), std::string::npos);
    EXPECT_NE(materialsPanel.find("shared Library metadata"), std::string::npos);
    EXPECT_NE(projectMaterial.find("saved with this carve operation"), std::string::npos);
    EXPECT_NE(projectMaterial.find("does not "), std::string::npos);
    EXPECT_NE(projectMaterial.find("change the Library model's shared material metadata"),
              std::string::npos);
    EXPECT_NE(projectPersistence.find("{\"material_id\""), std::string::npos);
    EXPECT_NE(projectPersistence.find("{\"material_name\""), std::string::npos);
}

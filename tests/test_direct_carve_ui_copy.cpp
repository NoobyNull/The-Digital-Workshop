#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readDirectCarvePanel() {
    std::ifstream file(std::string(CMAKE_SOURCE_DIR) + "/src/ui/panels/direct_carve_panel.cpp");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace

TEST(DirectCarveUiCopy, SeparatesMaterialBlankFromMachineTravel) {
    const std::string source = readDirectCarvePanel();

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

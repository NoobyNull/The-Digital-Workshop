#include <gtest/gtest.h>

#include "ui/dialogs/file_filter_utils.h"

using namespace dw;

TEST(FileDialogFilters, ConvertsGlobListToNativeSpec) {
    EXPECT_EQ(file_dialog::toNativeFilterSpec("*.stl;*.obj;*.3mf"), "stl,obj,3mf");
}

TEST(FileDialogFilters, ConvertsGCodeFiltersToNativeSpec) {
    EXPECT_EQ(file_dialog::toNativeFilterSpec("*.gcode;*.nc;*.ngc;*.tap"),
              "gcode,nc,ngc,tap");
}

TEST(FileDialogFilters, SkipsAllFilesFilter) {
    EXPECT_TRUE(file_dialog::toNativeFilterSpec("*.*").empty());
    EXPECT_TRUE(file_dialog::toNativeFilterSpec("*").empty());
}

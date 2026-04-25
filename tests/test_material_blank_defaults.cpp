#include <gtest/gtest.h>

#include "core/carve/material_blank_defaults.h"

TEST(MaterialBlankDefaults, UsesLoadedModelBoundsAsInitialBlank) {
    const dw::Vec3 min{10.0f, 20.0f, 5.0f};
    const dw::Vec3 max{265.8f, 420.0f, 28.0f};

    const auto blank = dw::carve::materialBlankFromModelBounds(min, max);

    EXPECT_FLOAT_EQ(blank.width, 255.8f);
    EXPECT_FLOAT_EQ(blank.height, 400.0f);
    EXPECT_FLOAT_EQ(blank.thickness, 23.0f);
}

TEST(MaterialBlankDefaults, KeepsDegenerateModelBoundsEditable) {
    const dw::Vec3 min{0.0f, 0.0f, 0.0f};
    const dw::Vec3 max{0.0f, -5.0f, 0.0f};

    const auto blank = dw::carve::materialBlankFromModelBounds(min, max);

    EXPECT_FLOAT_EQ(blank.width, 1.0f);
    EXPECT_FLOAT_EQ(blank.height, 1.0f);
    EXPECT_FLOAT_EQ(blank.thickness, 0.5f);
}

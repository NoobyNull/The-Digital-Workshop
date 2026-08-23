// Digital Workshop - Navigation style cycle tests

#include <gtest/gtest.h>

#include "core/config/config.h"

TEST(NavigationStyle, CyclesDefaultCadMaya) {
    EXPECT_EQ(dw::nextNavStyle(dw::NavStyle::Default), dw::NavStyle::CAD);
    EXPECT_EQ(dw::nextNavStyle(dw::NavStyle::CAD), dw::NavStyle::Maya);
    EXPECT_EQ(dw::nextNavStyle(dw::NavStyle::Maya), dw::NavStyle::Default);
}

TEST(NavigationStyle, UsesSingleLetterLabels) {
    EXPECT_STREQ(dw::navStyleLetter(dw::NavStyle::Default), "D");
    EXPECT_STREQ(dw::navStyleLetter(dw::NavStyle::CAD), "C");
    EXPECT_STREQ(dw::navStyleLetter(dw::NavStyle::Maya), "M");
}

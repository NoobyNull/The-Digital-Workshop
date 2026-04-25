#include "core/cnc/machine_units.h"
#include "core/cnc/unified_settings.h"

#include <gtest/gtest.h>

using namespace dw;

TEST(MachineUnits, EmptySettingsDefaultToMillimeters)
{
    UnifiedSettingsMap settings;

    EXPECT_EQ(cnc::sendUnitsFromUnifiedSettings(settings),
              cnc::SendUnits::Millimeters);
    EXPECT_STREQ(cnc::gcodeUnitMode(cnc::SendUnits::Millimeters), "G21");
}

TEST(MachineUnits, ReportInchesZeroUsesMillimeters)
{
    UnifiedSettingsMap settings;
    ASSERT_TRUE(settings.parseGrblLine("$13=0"));

    EXPECT_EQ(cnc::sendUnitsFromUnifiedSettings(settings),
              cnc::SendUnits::Millimeters);
}

TEST(MachineUnits, ReportInchesOneUsesInches)
{
    UnifiedSettingsMap settings;
    ASSERT_TRUE(settings.parseGrblLine("$13=1"));

    EXPECT_EQ(cnc::sendUnitsFromUnifiedSettings(settings),
              cnc::SendUnits::Inches);
    EXPECT_STREQ(cnc::gcodeUnitMode(cnc::SendUnits::Inches), "G20");
}

TEST(MachineUnits, InvalidReportInchesDefaultsToMillimeters)
{
    EXPECT_EQ(cnc::sendUnitsFromReportInchesValue(""),
              cnc::SendUnits::Millimeters);
    EXPECT_EQ(cnc::sendUnitsFromReportInchesValue("not-a-number"),
              cnc::SendUnits::Millimeters);
}

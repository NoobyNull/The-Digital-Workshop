#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "app/river_sign_study_command.h"

TEST(RiverSignStudyCommand, IsAbsentFromOrdinaryLaunches) {
    const auto command = dw::river_sign_study::parseCommand({"--verbose"});
    EXPECT_TRUE(command.valid());
    EXPECT_FALSE(command.requested);
    EXPECT_TRUE(command.fixtureDirectory.empty());
}

TEST(RiverSignStudyCommand, ParsesOneExplicitFixtureDirectory) {
    const auto command = dw::river_sign_study::parseCommand(
        {"--verbose", "--river-sign-study", "/tmp/river-sign"});
    ASSERT_TRUE(command.valid()) << command.error;
    EXPECT_TRUE(command.requested);
    EXPECT_EQ(command.fixtureDirectory, dw::Path("/tmp/river-sign"));
}

TEST(RiverSignStudyCommand, RejectsMissingOrRepeatedFixtureArguments) {
    const auto missing =
        dw::river_sign_study::parseCommand({"--river-sign-study"});
    EXPECT_FALSE(missing.valid());

    const auto optionInsteadOfPath = dw::river_sign_study::parseCommand(
        {"--river-sign-study", "--verbose"});
    EXPECT_FALSE(optionInsteadOfPath.valid());

    const auto shortDiagnosticInsteadOfPath =
        dw::river_sign_study::parseCommand({"--river-sign-study", "-d"});
    EXPECT_TRUE(shortDiagnosticInsteadOfPath.requested);
    EXPECT_FALSE(shortDiagnosticInsteadOfPath.valid());
    EXPECT_TRUE(shortDiagnosticInsteadOfPath.fixtureDirectory.empty());

    const auto repeated = dw::river_sign_study::parseCommand(
        {"--river-sign-study", "/tmp/one",
         "--river-sign-study", "/tmp/two"});
    EXPECT_FALSE(repeated.valid());
}

TEST(RiverSignStudyCommand, RejectsModesThatWouldBypassInteractiveStudyStartup) {
    for (const auto& arguments : {
             std::vector<std::string>{"--river-sign-study", "/tmp/fixture",
                                      "--diagnostic"},
             std::vector<std::string>{"-d", "--river-sign-study", "/tmp/fixture"},
             std::vector<std::string>{"--ux-capture", "guided-home",
                                      "--river-sign-study", "/tmp/fixture"},
         }) {
        const auto command = dw::river_sign_study::parseCommand(arguments);
        EXPECT_TRUE(command.requested);
        EXPECT_FALSE(command.valid());
    }
}

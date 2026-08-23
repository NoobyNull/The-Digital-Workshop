#include <gtest/gtest.h>

#include <array>

#include "modules/run_coordination/run_package.h"

namespace {

using namespace dw;
using namespace dw::run_coordination;

constexpr workshop::ProjectId kProject{41};
constexpr workshop::ProjectItemRef kModel{kProject, workshop::ProjectItemId{401}};
constexpr workshop::ProjectItemRef kOperation{kProject, workshop::ProjectItemId{402}};
constexpr workshop::ProjectItemRef kGCode{kProject, workshop::ProjectItemId{403}};
constexpr workshop::LibraryItemRef kSource{workshop::LibraryItemKind::Model,
                                           workshop::LibraryItemId{901}};

carve_preparation::PrepareCarvePin makePin(
    carve_preparation::PreparationToken token = {71}) {
    return {kProject,
            kModel,
            kSource,
            kOperation,
            token,
            carve_preparation::PreparationRevision{12}};
}

ToolpathIdentity makeToolpath(ToolpathRevision revision = {8},
                              std::string fingerprint = "sha256:gcode-v8") {
    return {kOperation, kGCode, revision, std::move(fingerprint)};
}

RunSetupIdentity makeSetup(
    carve_preparation::PrepareCarvePin pin = makePin(),
    ToolpathIdentity toolpath = makeToolpath()) {
    return {std::move(pin), std::move(toolpath)};
}

RunPackage makePackage(
    workshop::RunId run = workshop::RunId{51},
    RunSetupIdentity setup = makeSetup(),
    RunPreflightFacts facts = RunPreflightFacts::allSatisfied(),
    PreflightRevision preflightRevision = {3}) {
    RunPreflightSnapshot preflight(setup, preflightRevision, std::move(facts));
    return {RunIdentity(run, setup), std::move(preflight)};
}

} // namespace

TEST(RunPackage, AcceptsAnExactPinnedPreparationToolpathAndPreflightSnapshot) {
    const RunPackage package = makePackage();

    ASSERT_TRUE(package.valid());
    EXPECT_EQ(package.issue(), RunPackageIssue::None);
    EXPECT_EQ(package.identity().run(), workshop::RunId{51});
    EXPECT_TRUE(package.identity().setup().preparation() == makePin());
    EXPECT_EQ(package.identity().setup().toolpath().operationItem(), kOperation);
    EXPECT_EQ(package.identity().setup().toolpath().gcodeItem(), kGCode);
    EXPECT_EQ(package.identity().setup().toolpath().revision(), ToolpathRevision{8});
    EXPECT_EQ(package.identity().setup().toolpath().contentFingerprint(),
              "sha256:gcode-v8");
    EXPECT_TRUE(package.preflight().passed());
}

TEST(RunPackage, RejectsPreflightEvidenceFromAnotherExactSetup) {
    const RunSetupIdentity requested = makeSetup();
    const RunSetupIdentity checked = makeSetup(makePin({72}));
    const RunPackage package(
        RunIdentity(workshop::RunId{51}, requested),
        RunPreflightSnapshot(checked,
                             PreflightRevision{3},
                             RunPreflightFacts::allSatisfied()));

    EXPECT_FALSE(package.valid());
    EXPECT_EQ(package.issue(), RunPackageIssue::PreflightBindingMismatch);
}

TEST(RunPackage, RejectsMissingSafetyEvidenceAndReportsEveryMissingFact) {
    const RunPreflightFacts facts(
        std::array<bool, 7>{true, true, false, false, true, false, true});
    const auto missing = facts.missingChecks();

    ASSERT_EQ(missing.size(), 3U);
    EXPECT_EQ(missing[0], RunPreflightCheck::Homed);
    EXPECT_EQ(missing[1], RunPreflightCheck::WorkZeroSet);
    EXPECT_EQ(missing[2], RunPreflightCheck::StockSecured);
    EXPECT_EQ(makePackage(workshop::RunId{51}, makeSetup(), facts).issue(),
              RunPackageIssue::PreflightFailed);
}

TEST(RunPackage, RejectsInvalidRunAndPreflightRevisions) {
    EXPECT_EQ(makePackage(workshop::RunId{0}).issue(),
              RunPackageIssue::InvalidRunId);
    EXPECT_EQ(makePackage(workshop::RunId{51},
                          makeSetup(),
                          RunPreflightFacts::allSatisfied(),
                          PreflightRevision{0})
                  .issue(),
              RunPackageIssue::InvalidPreflightRevision);
}

TEST(RunPackage, RejectsToolpathsThatDoNotBelongToThePinnedOperation) {
    const workshop::ProjectItemRef otherOperation{kProject,
                                                   workshop::ProjectItemId{404}};
    const RunSetupIdentity setup(
        makePin(),
        ToolpathIdentity(otherOperation, kGCode, ToolpathRevision{8}, "sha256:other"));

    EXPECT_FALSE(setup.valid());
    EXPECT_EQ(makePackage(workshop::RunId{51}, setup).issue(),
              RunPackageIssue::SetupIdentityMismatch);
}

TEST(RunPackage, ToolpathIdentityIncludesItemRevisionAndContentFingerprint) {
    const ToolpathIdentity original = makeToolpath();
    const ToolpathIdentity changedRevision = makeToolpath(ToolpathRevision{9});
    const ToolpathIdentity changedBytes = makeToolpath(ToolpathRevision{8}, "sha256:changed");

    EXPECT_FALSE(original == changedRevision);
    EXPECT_FALSE(original == changedBytes);
    EXPECT_FALSE(changedRevision == changedBytes);
}

// Digital Workshop - slicer-style main tab policy tests

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "modules/workshop/main_tabs.h"

namespace {

using dw::workshop::blockReason;
using dw::workshop::isMachineTab;
using dw::workshop::kMainTabs;
using dw::workshop::MainTab;
using dw::workshop::MainTabBlock;
using dw::workshop::MainTabFacts;
using dw::workshop::mainTabBlock;
using dw::workshop::mainTabLabel;
using dw::workshop::PrimaryAction;
using dw::workshop::PrimaryActionBlock;
using dw::workshop::primaryActionBlock;

} // namespace

TEST(MainTabs, FollowTheSlicerOrderWithTheLibraryBeforePrepare) {
    std::vector<std::string> labels;
    for (const auto tab : kMainTabs)
        labels.emplace_back(mainTabLabel(tab));

    EXPECT_EQ(labels,
              (std::vector<std::string>{
                  "Home", "Library", "Prepare", "Preview", "Device", "Project"}));
    EXPECT_EQ(std::set<std::string>(labels.begin(), labels.end()).size(), labels.size());
}

TEST(MainTabs, OnlyDeviceIsTheMachineWorkspace) {
    for (const auto tab : kMainTabs)
        EXPECT_EQ(isMachineTab(tab), tab == MainTab::Device) << mainTabLabel(tab);
}

TEST(MainTabs, EveryTabIsOpenWhileTheMachineIsIdle) {
    const MainTabFacts facts;
    for (const auto tab : kMainTabs)
        EXPECT_EQ(mainTabBlock(tab, facts), MainTabBlock::None) << mainTabLabel(tab);
}

TEST(MainTabs, ARunningJobHoldsTheDeviceTab) {
    MainTabFacts facts;
    facts.active = MainTab::Device;
    facts.machineStreaming = true;

    for (const auto tab : kMainTabs) {
        const auto block = mainTabBlock(tab, facts);
        if (tab == MainTab::Device) {
            EXPECT_EQ(block, MainTabBlock::None);
        } else {
            EXPECT_EQ(block, MainTabBlock::MachineRunning) << mainTabLabel(tab);
            EXPECT_NE(std::string(blockReason(block)).find("Device"), std::string::npos);
        }
    }
}

TEST(MainTabs, GenerateWaitsForTheCamRebuild) {
    MainTabFacts facts;
    facts.gcodeLoaded = true;

    const auto block = primaryActionBlock(PrimaryAction::GenerateToolpaths, facts);
    EXPECT_EQ(block, PrimaryActionBlock::ToolpathGenerationUnavailable);
    EXPECT_NE(std::string(blockReason(block)).find("CAM"), std::string::npos);

    facts.toolpathGenerationAvailable = true;
    EXPECT_EQ(primaryActionBlock(PrimaryAction::GenerateToolpaths, facts),
              PrimaryActionBlock::None);
    facts.machineStreaming = true;
    EXPECT_EQ(primaryActionBlock(PrimaryAction::GenerateToolpaths, facts),
              PrimaryActionBlock::MachineRunning);
}

TEST(MainTabs, SendToMachineNeedsLoadedGCodeAndAnIdleMachine) {
    MainTabFacts facts;
    EXPECT_EQ(primaryActionBlock(PrimaryAction::SendToMachine, facts),
              PrimaryActionBlock::NoGCodeLoaded);

    facts.gcodeLoaded = true;
    EXPECT_EQ(primaryActionBlock(PrimaryAction::SendToMachine, facts), PrimaryActionBlock::None);

    facts.machineStreaming = true;
    EXPECT_EQ(primaryActionBlock(PrimaryAction::SendToMachine, facts),
              PrimaryActionBlock::MachineRunning);
}

TEST(MainTabs, EveryBlockExplainsItselfInWords) {
    EXPECT_STREQ(blockReason(MainTabBlock::None), "");
    EXPECT_STREQ(blockReason(PrimaryActionBlock::None), "");
    EXPECT_GT(std::string(blockReason(MainTabBlock::MachineRunning)).size(), 10U);
    for (const auto block : {PrimaryActionBlock::ToolpathGenerationUnavailable,
                             PrimaryActionBlock::NoGCodeLoaded,
                             PrimaryActionBlock::MachineRunning}) {
        EXPECT_GT(std::string(blockReason(block)).size(), 10U);
    }
}

#include "core/cam/cam_engine_client.h"
#include <gtest/gtest.h>

namespace dw::cam {

TEST(CamEngineClient, ParsesHealthyResponse) {
    const auto h = parseHealth(R"({"ok":true,"service":"dw-bridge","version":1})");
    ASSERT_TRUE(h.has_value());
    EXPECT_TRUE(h->ok);
    EXPECT_EQ(h->service, "dw-bridge");
    EXPECT_EQ(h->version, 1);
}

TEST(CamEngineClient, RejectsWrongServiceAndGarbage) {
    const auto wrong = parseHealth(R"({"ok":true,"service":"other","version":1})");
    ASSERT_TRUE(wrong.has_value());
    EXPECT_NE(wrong->service, "dw-bridge");
    EXPECT_FALSE(parseHealth("not json").has_value());
    EXPECT_FALSE(parseHealth("").has_value());
}

TEST(CamEngineClient, ParsesMachineList) {
    const auto machines = parseMachines(
        R"([{"id":"grbl","name":"GRBL 1.1","description":"d","fileExtension":"nc"}])");
    ASSERT_EQ(machines.size(), 1u);
    EXPECT_EQ(machines[0].id, "grbl");
    EXPECT_EQ(machines[0].name, "GRBL 1.1");
    EXPECT_EQ(machines[0].fileExtension, "nc");
}

TEST(CamEngineClient, ParsesJobSuccessAndFailure) {
    const auto ok = parseJobResult(R"({"ok":true,"gcode":"G21\nG90\n"})");
    EXPECT_TRUE(ok.ok);
    EXPECT_EQ(ok.gcode, "G21\nG90\n");
    const auto bad = parseJobResult(R"({"ok":false,"error":"boom"})");
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.error, "boom");
    const auto garbage = parseJobResult("not json");
    EXPECT_FALSE(garbage.ok);
    EXPECT_FALSE(garbage.error.empty());
}

} // namespace dw::cam

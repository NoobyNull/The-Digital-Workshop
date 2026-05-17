#include <gtest/gtest.h>

#include "core/ai/ollama_runtime.h"

TEST(OllamaRuntime, BuildsPrivateLoopbackChatEndpoint) {
    dw::OllamaRuntimeConfig config;
    config.host = "127.0.0.1";
    config.port = 42319;

    EXPECT_EQ(dw::ollama::chatCompletionsEndpoint(config),
              "http://127.0.0.1:42319/v1/chat/completions");
}

TEST(OllamaRuntime, ParsesModelPresenceFromTagsResponse) {
    const std::string tags = R"({
        "models": [
            {"name": "llava:latest"},
            {"name": "qwen2.5vl:7b"}
        ]
    })";

    EXPECT_TRUE(dw::ollama::tagsResponseHasModel(tags, "llava:latest"));
    EXPECT_FALSE(dw::ollama::tagsResponseHasModel(tags, "missing:latest"));
}

TEST(OllamaRuntime, MissingModelReturnsPullCommand) {
    auto result = dw::OllamaRuntimeStatus::modelMissing("llava:latest");

    EXPECT_FALSE(result.ready);
    EXPECT_EQ(result.reason, "Ollama model is not installed");
    EXPECT_EQ(result.actionCommand, "ollama pull llava:latest");
}

TEST(OllamaRuntime, AutoPullsMissingModelsByDefault) {
    dw::OllamaRuntimeConfig config;

    EXPECT_TRUE(config.autoPullMissingModel);
}

TEST(OllamaRuntime, BuildsPullCommandForConfiguredModel) {
    EXPECT_EQ(dw::ollama::pullCommand("llava:latest"), "ollama pull llava:latest");
}

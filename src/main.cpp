// Digital Workshop - Main Entry Point

#include "app/application.h"
#include "app/river_sign_study_command.h"
#include "core/utils/log.h"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
volatile std::sig_atomic_t g_terminationRequested = 0;

void signalHandler(int signal) {
    g_terminationRequested = signal;
}
} // namespace

int main(int argc, char* argv[]) {
    bool diagnosticMode = false;
    const std::vector<std::string> arguments(argv + 1, argv + argc);
    const auto riverSignStudy = dw::river_sign_study::parseCommand(arguments);
    if (!riverSignStudy.valid()) {
        std::fprintf(stderr, "DW_STUDY_ERROR=%s\n", riverSignStudy.error.c_str());
        return 2;
    }
    if (riverSignStudy.requested)
        dw::log::setConsoleOutput(true);
#ifdef DW_ENABLE_UX_CAPTURE
    std::string uxCaptureScenario;
    dw::Path uxCaptureOutput;
    int uxCaptureHoldMilliseconds = 15000;
#endif

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verbose") == 0 ||
            std::strcmp(argv[i], "-v") == 0 ||
            std::strcmp(argv[i], "-V") == 0) {
            dw::log::setConsoleOutput(true);
        } else if (std::strcmp(argv[i], "--diagnostic") == 0 ||
                   std::strcmp(argv[i], "-d") == 0) {
            diagnosticMode = true;
            dw::log::setConsoleOutput(true);
#ifdef DW_ENABLE_UX_CAPTURE
        } else if (std::strcmp(argv[i], "--ux-capture") == 0 && i + 1 < argc) {
            uxCaptureScenario = argv[++i];
            dw::log::setConsoleOutput(true);
        } else if (std::strcmp(argv[i], "--ux-capture-hold-ms") == 0 &&
                   i + 1 < argc) {
            uxCaptureHoldMilliseconds = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--ux-capture-output") == 0 &&
                   i + 1 < argc) {
            uxCaptureOutput = argv[++i];
#endif
        }
    }

    dw::Application app;
    app.setTerminationSignalFlag(&g_terminationRequested);

    std::signal(SIGTERM, signalHandler);
    std::signal(SIGINT, signalHandler);

    if (!app.init(diagnosticMode)) {
        return 1;
    }

    if (diagnosticMode) {
        return 0;  // Exit after successful initialization
    }

    if (riverSignStudy.requested) {
        return app.runRiverSignStudy(riverSignStudy.fixtureDirectory);
    }

#ifdef DW_ENABLE_UX_CAPTURE
    if (!uxCaptureScenario.empty()) {
        return app.runUxCapture(
            uxCaptureScenario, uxCaptureHoldMilliseconds, uxCaptureOutput);
    }
#endif

    int result = app.run();
    return result;
}

// Digital Workshop - Main Entry Point

#include "app/application.h"
#include "core/utils/log.h"

#include <csignal>
#include <cstdio>
#include <cstring>

namespace {
volatile std::sig_atomic_t g_terminationRequested = 0;

void signalHandler(int signal) {
    g_terminationRequested = signal;
}
} // namespace

int main(int argc, char* argv[]) {
    bool diagnosticMode = false;

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

    int result = app.run();
    return result;
}

#include <gtest/gtest.h>

#include "ui/dialogs/import_options_dialog.h"
#include "ui/dialogs/import_summary_dialog.h"
#include "ui/dialogs/tagger_shutdown_dialog.h"

namespace {

void expectCleanExit(void (*body)()) {
    ASSERT_EXIT(
        {
            body();
            std::_Exit(0);
        },
        testing::ExitedWithCode(0),
        "");
}

} // namespace

TEST(ModalDialogs, ImportOptionsOpenDoesNotRequireActiveImGuiFrame) {
    expectCleanExit([] {
        dw::ImportOptionsDialog dialog;
        dialog.open({});
    });
}

TEST(ModalDialogs, ImportSummaryOpenDoesNotRequireActiveImGuiFrame) {
    expectCleanExit([] {
        dw::ImportSummaryDialog dialog;
        dw::ImportBatchSummary summary;
        dialog.open(summary);
    });
}

TEST(ModalDialogs, TaggerShutdownOpenDoesNotRequireActiveImGuiFrame) {
    expectCleanExit([] {
        dw::TaggerShutdownDialog dialog;
        dialog.open(nullptr);
    });
}

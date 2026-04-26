#pragma once

// Digital Workshop - Import Summary Dialog
// Modal dialog displaying batch import results with interactive duplicate review.

#include <functional>
#include <vector>

#include "core/import/import_task.h"
#include "ui/dialogs/dialog.h"

namespace dw {

class ImportSummaryDialog : public Dialog {
  public:
    ImportSummaryDialog();
    ~ImportSummaryDialog() override = default;

    // Open the dialog with import summary data
    void open(const ImportBatchSummary& summary);

    // Render the dialog (called each frame by UIManager)
    void render() override;

    // Set callback for re-importing selected duplicates
    using ReimportCallback = std::function<void(std::vector<DuplicateRecord>)>;
    void setOnReimport(ReimportCallback callback);

  private:
    ImportBatchSummary m_summary;
    std::vector<uint8_t>
        m_checked; // per-duplicate checkbox state (uint8_t avoids vector<bool> issues)
    bool m_pendingOpen = false;
    ReimportCallback m_onReimport;
};

} // namespace dw

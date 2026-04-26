#pragma once

// Digital Workshop - Import Options Dialog
// Modal dialog for selecting file handling mode during import.
// Auto-detects network filesystems and recommends copy mode.

#include <functional>
#include <vector>

#include "core/config/config.h"
#include "core/import/filesystem_detector.h"
#include "ui/dialogs/dialog.h"

namespace dw {

class ImportOptionsDialog : public Dialog {
  public:
    using ResultCallback =
        std::function<void(FileHandlingMode mode, bool tagAfterImport, const std::vector<Path>& paths)>;

    ImportOptionsDialog();
    ~ImportOptionsDialog() override = default;

    // Open dialog with files to import; runs filesystem detection
    void open(const std::vector<Path>& paths);

    // Render the modal (called each frame by UIManager)
    void render() override;

    // Set callback invoked when user confirms import
    void setOnConfirm(ResultCallback callback);

  private:
    std::vector<Path> m_paths;
    StorageLocation m_detectedLocation = StorageLocation::Unknown;
    int m_selectedMode = 0; // maps to FileHandlingMode
    bool m_queueForTagging = false;
    bool m_pendingOpen = false;
    ResultCallback m_onConfirm;
};

} // namespace dw

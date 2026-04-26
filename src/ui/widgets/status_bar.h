#pragma once

// Digital Workshop - Status Bar Widget
// Bottom-anchored widget showing import progress during active import,
// general status when idle. Not a Panel subclass — rendered directly by UIManager.

#include <functional>
#include <string>
#include <vector>

namespace dw {

// Forward declarations
struct ImportProgress;
struct LoadingState;

class StatusBar {
  public:
    StatusBar() = default;
    ~StatusBar() = default;

    // Render the status bar. Call each frame from UIManager.
    void render(const LoadingState* loadingState);

    // Set import progress reference (called when import starts)
    void setImportProgress(const ImportProgress* progress);

    // Clear import progress (called when batch completes)
    void clearImportProgress();

    // Set cancel callback (wired by Application to ImportQueue::cancel)
    void setOnCancel(std::function<void()> callback) { m_onCancel = std::move(callback); }

    // Set idle context tips shown when no loading/import work is active.
    void setContextTips(std::vector<std::string> tips) { m_contextTips = std::move(tips); }

    // Set callback for the always-available Tool Library shortcut.
    void setOnOpenToolLibrary(std::function<void()> callback) {
        m_onOpenToolLibrary = std::move(callback);
    }

  private:
    void renderContextTip() const;

    const ImportProgress* m_progress = nullptr;
    std::function<void()> m_onCancel;
    std::function<void()> m_onOpenToolLibrary;
    std::vector<std::string> m_contextTips;
};

} // namespace dw

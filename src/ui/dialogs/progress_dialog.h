#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "dialog.h"

namespace dw {

// Modal progress dialog for batch operations.
// Thread-safe: worker threads call advance()/isCancelled(), main thread calls render().
class ProgressDialog : public Dialog {
  public:
    ProgressDialog();
    ~ProgressDialog() override = default;

    void render() override;

    // Start a batch operation — opens the modal next frame
    void start(const std::string& title, int total, bool cancellable = true);

    // Thread-safe: called by worker threads to advance progress
    void advance(const std::string& currentItem = "");

    // Thread-safe: update status text without advancing a known-size batch
    void setStatus(const std::string& currentItem);

    // Thread-safe: check if user pressed Cancel
    bool isCancelled() const;

    // Called when batch is fully complete (closes the dialog)
    void finish();

  private:
    std::atomic<int> m_completed{0};
    std::atomic<int> m_total{0};
    std::atomic<bool> m_cancelled{false};
    bool m_cancellable = true;
    bool m_pendingOpen = false;
    std::mutex m_mutex;
    std::string m_currentItem; // protected by m_mutex
};

} // namespace dw

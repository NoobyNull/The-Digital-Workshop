#pragma once

#include <functional>

#include "dialog.h"

namespace dw {

struct TaggerProgress;

class TaggerShutdownDialog : public Dialog {
  public:
    TaggerShutdownDialog();
    ~TaggerShutdownDialog() override = default;

    void open(const TaggerProgress* progress);
    void render() override;
    void setOnQuit(std::function<void()> callback);

  private:
    const TaggerProgress* m_progress = nullptr;
    bool m_pendingOpen = false;
    std::function<void()> m_onQuit;
};

} // namespace dw

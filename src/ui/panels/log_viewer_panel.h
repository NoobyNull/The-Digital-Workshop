#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "panel.h"

namespace dw {

[[nodiscard]] std::vector<std::string> readLogTail(const std::filesystem::path& path,
                                                   std::size_t maxLines);
[[nodiscard]] std::vector<std::string> filterLogLines(const std::vector<std::string>& lines,
                                                      const std::string& filter);

class LogViewerPanel : public Panel {
  public:
    LogViewerPanel();

    void render() override;
    void refresh();

  private:
    struct LogSource {
        std::string label;
        std::filesystem::path path;
        std::vector<std::string> lines;
    };

    void refreshSource(LogSource& source);
    void renderToolbar();
    void renderSource(LogSource& source);
    [[nodiscard]] std::string visibleText(const std::vector<std::string>& lines) const;

    std::vector<LogSource> m_sources;
    int m_activeSource = 0;
    char m_filter[128]{};
    bool m_autoRefresh = true;
    bool m_autoScroll = true;
    float m_refreshTimer = 0.0f;
    std::size_t m_maxLines = 1000;
};

} // namespace dw

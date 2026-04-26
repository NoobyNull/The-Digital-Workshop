#include "log_viewer_panel.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>
#include <sstream>

#include <imgui.h>

#include "core/config/config.h"
#include "core/paths/app_paths.h"

namespace dw {

namespace {

std::string lowerCopy(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

} // namespace

std::vector<std::string> readLogTail(const std::filesystem::path& path, std::size_t maxLines) {
    if (maxLines == 0)
        return {};
    if (!std::filesystem::exists(path)) {
        return {"Log file not found: " + path.string()};
    }

    std::ifstream in(path);
    if (!in.is_open()) {
        return {"Could not open log file: " + path.string()};
    }

    std::deque<std::string> tail;
    std::string line;
    while (std::getline(in, line)) {
        tail.push_back(line);
        if (tail.size() > maxLines)
            tail.pop_front();
    }

    return {tail.begin(), tail.end()};
}

std::vector<std::string> filterLogLines(const std::vector<std::string>& lines,
                                        const std::string& filter) {
    if (filter.empty())
        return lines;

    std::vector<std::string> result;
    std::string needle = lowerCopy(filter);
    for (const auto& line : lines) {
        if (lowerCopy(line).find(needle) != std::string::npos)
            result.push_back(line);
    }
    return result;
}

LogViewerPanel::LogViewerPanel() : Panel("Log Viewer") {
    m_open = false;
    m_sources = {
        {"Tagger", paths::getDataDir() / "tagger.log", {}},
        {"Application", paths::getLogPath(), {}},
        {"Import", Config::instance().getSupportDir() / ".import-log", {}},
    };
}

void LogViewerPanel::refreshSource(LogSource& source) {
    source.lines = readLogTail(source.path, m_maxLines);
}

void LogViewerPanel::refresh() {
    for (auto& source : m_sources)
        refreshSource(source);
}

std::string LogViewerPanel::visibleText(const std::vector<std::string>& lines) const {
    std::ostringstream out;
    for (const auto& line : lines)
        out << line << '\n';
    return out.str();
}

void LogViewerPanel::renderToolbar() {
    if (ImGui::Button("Refresh")) {
        refresh();
        m_refreshTimer = 0.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear View")) {
        for (auto& source : m_sources)
            source.lines.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto", &m_autoRefresh);
    ImGui::SameLine();
    ImGui::Checkbox("Follow", &m_autoScroll);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    int maxLines = static_cast<int>(m_maxLines);
    if (ImGui::InputInt("Lines", &maxLines, 100, 500)) {
        maxLines = std::clamp(maxLines, 100, 10000);
        m_maxLines = static_cast<std::size_t>(maxLines);
        refresh();
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##LogFilter", "Filter visible log lines", m_filter,
                                 sizeof(m_filter))) {
        m_autoScroll = true;
    }
}

void LogViewerPanel::renderSource(LogSource& source) {
    auto visible = filterLogLines(source.lines, m_filter);

    ImGui::TextDisabled("%s", source.path.string().c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy Visible")) {
        auto text = visibleText(visible);
        ImGui::SetClipboardText(text.c_str());
    }

    ImGui::Separator();
    if (ImGui::BeginChild("##LogLines", ImVec2(0, 0), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                ImGui::TextUnformatted(visible[static_cast<std::size_t>(i)].c_str());
        }
        if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void LogViewerPanel::render() {
    if (!m_open)
        return;

    applyMinSize(80.0f, 24.0f);
    if (!ImGui::Begin(m_title.c_str(), &m_open)) {
        ImGui::End();
        return;
    }

    if (m_sources[static_cast<std::size_t>(m_activeSource)].lines.empty())
        refreshSource(m_sources[static_cast<std::size_t>(m_activeSource)]);

    if (m_autoRefresh) {
        m_refreshTimer += ImGui::GetIO().DeltaTime;
        if (m_refreshTimer >= 1.0f) {
            refreshSource(m_sources[static_cast<std::size_t>(m_activeSource)]);
            m_refreshTimer = 0.0f;
        }
    }

    renderToolbar();
    ImGui::Separator();

    if (ImGui::BeginTabBar("##LogSources")) {
        for (std::size_t i = 0; i < m_sources.size(); ++i) {
            auto& source = m_sources[i];
            if (ImGui::BeginTabItem(source.label.c_str())) {
                m_activeSource = static_cast<int>(i);
                if (source.lines.empty())
                    refreshSource(source);
                renderSource(source);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace dw

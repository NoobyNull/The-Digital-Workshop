#pragma once

#include <string>

#include <imgui.h>

#include "core/utils/thread_utils.h"

namespace dw {

// Base class for all UI panels
class Panel {
  public:
    Panel(const std::string& title) : m_title(title) {}
    virtual ~Panel() = default;

    // Called each frame to render the panel (main thread only)
    virtual void render() = 0;

    // Panel state
    bool isOpen() const { return m_open; }
    void setOpen(bool open) { m_open = open; }
    void toggle() { m_open = !m_open; }

    const std::string& title() const { return m_title; }

    // A page window is placed by a main tab's fixed layout and has no close
    // button; a floating window (Materials, Tool Library) keeps one.
    void setPageWindow(bool pageWindow) { m_pageWindow = pageWindow; }

  protected:
    // Pass to ImGui::Begin as p_open.
    bool* closeHandle() { return m_pageWindow ? nullptr : &m_open; }

    // Apply minimum window size constraints before ImGui::Begin().
    // widthChars/heightLines are in font-relative units for DPI scaling.
    void applyMinSize(float widthChars, float heightLines) const {
        float fontSize = ImGui::GetFontSize();
        float minW = fontSize * widthChars;
        float minH = ImGui::GetTextLineHeightWithSpacing() * heightLines;
        ImGui::SetNextWindowSizeConstraints(ImVec2(minW, minH),
                                            ImVec2(FLT_MAX, FLT_MAX));
    }

    std::string m_title;
    bool m_open = true;
    bool m_pageWindow = false;
};

} // namespace dw

// Digital Workshop - UI Manager (page host & layouts)
// One fixed page per main tab, laid out like a 3D-printing slicer: a settings
// column beside a large canvas. Pages are ImGui dock layouts rebuilt when the
// tab changes and locked so windows cannot be undocked or rearranged.

#include "managers/ui_manager.h"

#include <cstring>

#include <imgui.h>
#include <imgui_internal.h>

#include "core/config/config.h"
#include "modules/workshop/ui/workshop_layout_metrics.h"
#include "ui/panels/viewport_panel.h"

namespace dw {
namespace {

using workshop::MainTab;
namespace metrics = workshop::ui;

// Shared by every node of the page dockspace: no undocking, no docking
// floating windows into the page, and no per-node window menus or close
// buttons. Splitters stay draggable.
constexpr ImGuiDockNodeFlags kPageDockFlags =
    ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoDocking |
    ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton;

// Split `remaining` and return the part on `dir`; `remaining` keeps the rest.
ImGuiID splitOff(ImGuiID& remaining, ImGuiDir dir, float ratio) {
    ImGuiID part = 0;
    ImGui::DockBuilderSplitNode(remaining, dir, ratio, &part, &remaining);
    return part;
}

// Canvases and full pages need no tab strip; sections keep theirs as a header.
void hideTabBar(ImGuiID nodeId) {
    if (auto* node = ImGui::DockBuilderGetNode(nodeId))
        node->SetLocalFlags(node->LocalFlags | ImGuiDockNodeFlags_NoTabBar);
}

} // namespace

// A tabbed section opens on `windowTitle` rather than on whichever window
// ImGui added last. ImGui adds tabs over the first frames after a layout is
// built, so the choice is applied once they all exist (applyPendingSectionTabs).
void UIManager::openSectionOn(ImGuiID nodeId, const char* windowTitle) {
    // Mirrors ImGuiWindow::TabId, which is GetID("#TAB") inside the window.
    m_pendingSectionTabs.emplace_back(nodeId, ImHashStr("#TAB", 0, ImHashStr(windowTitle)));
}

void UIManager::applyPendingSectionTabs() {
    // ImGui focuses every window the first time it appears, and a dock node
    // selects the focused window's tab. Hand focus to the page's main window
    // once the new page has appeared so section defaults hold.
    if (m_focusAfterLayout) {
        if (const auto* entry = findPanel(m_focusAfterLayout))
            ImGui::SetWindowFocus(entry->windowTitle);
        m_focusAfterLayout = nullptr;
    }
    auto pending = m_pendingSectionTabs.begin();
    while (pending != m_pendingSectionTabs.end()) {
        // A node gains one tab per frame-update, and ImGui selects each newly
        // added tab; wait until every docked window has its tab.
        auto* node = ImGui::DockBuilderGetNode(pending->first);
        if (node && node->TabBar && node->TabBar->Tabs.Size >= node->Windows.Size &&
            ImGui::TabBarFindTabByID(node->TabBar, pending->second)) {
            node->TabBar->NextSelectedTabId = pending->second;
            pending = m_pendingSectionTabs.erase(pending);
        } else {
            ++pending;
        }
    }
}

const std::vector<const char*>& UIManager::pageWindowKeys(MainTab tab) {
    static const std::vector<const char*> home = {"start_page"};
    static const std::vector<const char*> library = {"library", "viewport", "properties"};
    static const std::vector<const char*> prepare = {
        "cnc_tool", "project", "direct_carve", "properties", "viewport"};
    static const std::vector<const char*> preview = {"gcode", "viewport"};
    static const std::vector<const char*> device = {
        "viewport", "gcode", "cnc_job", "cnc_console", "cnc_status", "cnc_jog",
        "cnc_safety", "cnc_wcs", "cnc_tool", "cnc_macros", "cnc_settings"};
    static const std::vector<const char*> project = {"project"};
    switch (tab) {
    case MainTab::Home:
        return home;
    case MainTab::Library:
        return library;
    case MainTab::Prepare:
        return prepare;
    case MainTab::Preview:
        return preview;
    case MainTab::Device:
        return device;
    case MainTab::Project:
        return project;
    }
    return home;
}

MainTab UIManager::defaultTabForWindow(const std::string& key) {
    if (key == "start_page")
        return MainTab::Home;
    if (key == "library")
        return MainTab::Library;
    if (key == "gcode")
        return MainTab::Preview;
    if (key.rfind("cnc_", 0) == 0 && key != "cnc_tool")
        return MainTab::Device;
    return MainTab::Prepare;
}

void UIManager::renderPageHost() {
    // The page fills the work area left between the top bar and status bar.
    const auto* viewport = ImGui::GetMainViewport();
    const float statusBarH = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2;

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - statusBarH));
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpaceHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0F, 0.0F), kPageDockFlags);
    ImGui::End();

    if (m_layoutDirty) {
        buildPageLayout(dockspaceId);
        m_layoutDirty = false;
    }
}

void UIManager::dockPanel(const char* key, ImGuiID node) const {
    if (const auto* entry = findPanel(key))
        ImGui::DockBuilderDockWindow(entry->windowTitle, node);
}

void UIManager::buildPageLayout(ImGuiID dockspaceId) {
    const auto* viewport = ImGui::GetMainViewport();
    const float width = viewport->WorkSize.x;
    const float em = ImGui::GetFontSize();

    m_pendingSectionTabs.clear();
    m_focusAfterLayout = "viewport";
    if (m_activeTab == MainTab::Home)
        m_focusAfterLayout = "start_page";
    else if (m_activeTab == MainTab::Project)
        m_focusAfterLayout = "project";
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID canvas = dockspaceId;
    switch (m_activeTab) {
    case MainTab::Home:
    case MainTab::Project:
        dockPanel(m_activeTab == MainTab::Home ? "start_page" : "project", canvas);
        hideTabBar(canvas);
        break;
    case MainTab::Library: {
        const auto column = metrics::chooseSideColumn(width, em, metrics::kLibraryBrowser);
        const ImGuiID browser = splitOff(canvas, ImGuiDir_Left, column.splitRatio);
        const ImGuiID details = splitOff(canvas, ImGuiDir_Down, 0.40F);
        dockPanel("library", browser);
        dockPanel("viewport", canvas);
        dockPanel("properties", details);
        hideTabBar(browser);
        hideTabBar(canvas);
        break;
    }
    case MainTab::Prepare: {
        // Slicer sidebar: printer/filament → tool & material, object list →
        // project contents, process (Global | Objects) → CAM | Properties.
        const auto column = metrics::chooseSideColumn(width, em, metrics::kSettingsSidebar);
        ImGuiID sidebar = splitOff(canvas, ImGuiDir_Left, column.splitRatio);
        const ImGuiID toolSection = splitOff(sidebar, ImGuiDir_Up, 0.24F);
        const ImGuiID processSection = splitOff(sidebar, ImGuiDir_Down, 0.42F);
        dockPanel("cnc_tool", toolSection);
        dockPanel("project", sidebar);
        dockPanel("direct_carve", processSection);
        dockPanel("properties", processSection);
        openSectionOn(processSection, "CAM");
        dockPanel("viewport", canvas);
        hideTabBar(canvas);
        break;
    }
    case MainTab::Preview: {
        const auto column = metrics::chooseSideColumn(width, em, metrics::kSettingsSidebar);
        const ImGuiID sidebar = splitOff(canvas, ImGuiDir_Left, column.splitRatio);
        dockPanel("gcode", sidebar);
        dockPanel("viewport", canvas);
        hideTabBar(canvas);
        break;
    }
    case MainTab::Device: {
        // Bambu's Device tab: the live view with the job underneath, and the
        // machine controls in a column on the right.
        const auto column =
            metrics::chooseSideColumn(width, em, metrics::kMachineControlColumn);
        ImGuiID controls = splitOff(canvas, ImGuiDir_Right, column.splitRatio);
        const ImGuiID jobArea = splitOff(canvas, ImGuiDir_Down, 0.38F);
        const ImGuiID status = splitOff(controls, ImGuiDir_Up, 0.34F);
        const ImGuiID jog = splitOff(controls, ImGuiDir_Up, 0.45F);
        dockPanel("viewport", canvas);
        hideTabBar(canvas);
        for (const char* key : {"gcode", "cnc_job", "cnc_console"})
            dockPanel(key, jobArea);
        openSectionOn(jobArea, "G-code");
        dockPanel("cnc_status", status);
        dockPanel("cnc_jog", jog);
        for (const char* key : {"cnc_safety", "cnc_wcs", "cnc_tool", "cnc_macros",
                                "cnc_settings"})
            dockPanel(key, controls);
        openSectionOn(controls, "Safety");
        break;
    }
    }

    ImGui::DockBuilderFinish(dockspaceId);
}

void UIManager::restoreVisibilityFromConfig() {
    // Page windows follow the tab; only floating windows remember themselves.
    auto& cfg = Config::instance();
    m_showMaterials = cfg.getShowMaterials();
    m_showToolBrowser = cfg.getShowToolBrowser();
    applyTabToPanels();
}

void UIManager::saveVisibilityToConfig() {
    auto& cfg = Config::instance();
    cfg.setShowMaterials(m_showMaterials);
    cfg.setShowToolBrowser(m_showToolBrowser);
}

void UIManager::applyRenderSettingsFromConfig() {
    if (!m_viewportPanel)
        return;

    auto& cfg = Config::instance();
    auto& rs = m_viewportPanel->renderSettings();
    rs.lightDir = cfg.getRenderLightDir();
    rs.lightColor = cfg.getRenderLightColor();
    rs.ambient = cfg.getRenderAmbient();
    rs.objectColor = cfg.getRenderObjectColor();
    rs.shininess = cfg.getRenderShininess();
    rs.showGrid = cfg.getShowGrid();
    rs.showAxis = cfg.getShowAxis();
}

} // namespace dw

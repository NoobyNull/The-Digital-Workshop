#include "settings_app.h"

#include <cstdio>
#include <cstdlib>

#include <SDL.h>
#include <glad/gl.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include "../src/core/paths/app_paths.h"
#include "../src/core/threading/thread_pool.h"
#include "../src/core/utils/file_utils.h"
#include "../src/core/utils/log.h"
#include "../src/ui/theme.h"
#include "../src/ui/ui_colors.h"

#include <cstring>
#include <filesystem>

namespace dw {

SettingsApp::SettingsApp() = default;

SettingsApp::~SettingsApp() {
    shutdown();
}

bool SettingsApp::init() {
    if (m_initialized)
        return true;

    // Load config first so ensureDirectoriesExist() can read category dirs
    Config::instance().load();
    paths::ensureDirectoriesExist();

    // Cache values for editing
    auto& cfg = Config::instance();
    m_themeIndex = cfg.getThemeIndex();
    m_uiScale = cfg.getUiScale();
    m_showGrid = cfg.getShowGrid();
    m_showAxis = cfg.getShowAxis();
    m_logLevel = cfg.getLogLevel();
    m_showStartPage = cfg.getShowStartPage();
    m_autoOrient = cfg.getAutoOrient();
    m_invertOrbitX = cfg.getInvertOrbitX();
    m_invertOrbitY = cfg.getInvertOrbitY();
    m_navStyle = cfg.getNavStyleIndex();
    m_enableFloatingWindows = cfg.getEnableFloatingWindows();
    m_lightDir = cfg.getRenderLightDir();
    m_lightColor = cfg.getRenderLightColor();
    m_ambient = cfg.getRenderAmbient();

    Color objColor = cfg.getRenderObjectColor();
    m_objectColor[0] = objColor.r;
    m_objectColor[1] = objColor.g;
    m_objectColor[2] = objColor.b;

    m_shininess = cfg.getRenderShininess();

    for (int i = 0; i < static_cast<int>(BindAction::COUNT); ++i) {
        m_bindings[static_cast<size_t>(i)] = cfg.getBinding(static_cast<BindAction>(i));
    }

    // Import settings
    m_parallelismTier = static_cast<int>(cfg.getParallelismTier());
    m_fileHandlingMode = static_cast<int>(cfg.getFileHandlingMode());
    m_showImportErrorToasts = cfg.getShowImportErrorToasts();

    Path libraryPath = cfg.getLibraryDir();
    if (libraryPath.empty()) {
        // Default to app data dir / "library"
        libraryPath = paths::getDataDir() / "library";
    }
    std::strncpy(m_libraryDir, libraryPath.string().c_str(), sizeof(m_libraryDir) - 1);
    m_libraryDir[sizeof(m_libraryDir) - 1] = '\0';

    // Workspace root
    auto copyDir = [](char* buf, size_t sz, const Path& dir) {
        std::strncpy(buf, dir.string().c_str(), sz - 1);
        buf[sz - 1] = '\0';
    };
    copyDir(m_workspaceRoot, sizeof(m_workspaceRoot), cfg.getWorkspaceRoot());

    // Category directories
    copyDir(m_modelsDir, sizeof(m_modelsDir), cfg.getModelsDir());
    copyDir(m_projectsDir, sizeof(m_projectsDir), cfg.getProjectsDir());
    copyDir(m_materialsDir, sizeof(m_materialsDir), cfg.getMaterialsDir());
    copyDir(m_gcodeDir, sizeof(m_gcodeDir), cfg.getGCodeDir());
    copyDir(m_supportDir, sizeof(m_supportDir), cfg.getSupportDir());

    // Local AI service
    std::strncpy(m_lmStudioEndpoint, cfg.getLMStudioEndpoint().c_str(), sizeof(m_lmStudioEndpoint) - 1);
    m_lmStudioEndpoint[sizeof(m_lmStudioEndpoint) - 1] = '\0';

    // Initialize SDL2
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    m_window = SDL_CreateWindow(
        "Digital Workshop - Settings", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, INITIAL_WIDTH,
        INITIAL_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);

    if (!m_window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_MakeCurrent(m_window, m_glContext);
    SDL_GL_SetSwapInterval(1);

    if (gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)) == 0) {
        std::fprintf(stderr, "gladLoadGL failed\n");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Load Inter as primary font
    {
#include "ui/fonts/inter_regular.h"
        ImFontConfig fontCfg;
        fontCfg.OversampleH = 2;
        fontCfg.OversampleV = 1;
        io.Fonts->AddFontFromMemoryCompressedBase85TTF(inter_regular_compressed_data_base85, 16.0f,
                                                       &fontCfg);
    }

    applyThemePreview();

    ImGui_ImplSDL2_InitForOpenGL(m_window, m_glContext);
    ImGui_ImplOpenGL3_Init("#version 330");

    m_initialized = true;
    return true;
}

int SettingsApp::run() {
    if (!m_initialized)
        return 1;

    m_running = true;
    while (m_running) {
        processEvents();
        render();
    }

    // Auto-save on close if dirty
    if (m_dirty) {
        applySettings();
    }

    return 0;
}

void SettingsApp::processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        ImGui_ImplSDL2_ProcessEvent(&event);

        if (event.type == SDL_QUIT) {
            m_running = false;
        }
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) {
            m_running = false;
        }
    }
}

void SettingsApp::render() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Full-window settings panel
    ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    if (ImGui::Begin("##Settings", nullptr, winFlags)) {
        ImGui::Text("Settings");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("SettingsTabs")) {
            if (ImGui::BeginTabItem("General")) {
                renderGeneralTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Appearance")) {
                renderAppearanceTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Rendering")) {
                renderRenderingTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Import")) {
                renderImportTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Bindings")) {
                renderBindingsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Paths")) {
                renderPathsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("About")) {
                renderAboutTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // Bottom bar with Apply/Close
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float contentWidth = ImGui::GetContentRegionAvail().x;
        float buttonWidth = 100.0f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;

        ImGui::SetCursorPosX(contentWidth - buttonWidth * 2 - spacing);

        if (ImGui::Button("Apply", ImVec2(buttonWidth, 0))) {
            applySettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(buttonWidth, 0))) {
            m_running = false;
        }
    }
    ImGui::End();

    ImGui::Render();

    // Auto-size: after a few frames let ImGui settle, then resize SDL window to fit content
    if (m_framesToAutoSize > 0) {
        --m_framesToAutoSize;
        if (m_framesToAutoSize == 0) {
            ImGuiWindow* imWin = ImGui::FindWindowByName("##Settings");
            if (imWin) {
                // ContentSize reflects the actual laid-out content dimensions
                int newW = static_cast<int>(imWin->ContentSize.x +
                                            ImGui::GetStyle().WindowPadding.x * 2.0f);
                int newH = static_cast<int>(imWin->ContentSize.y +
                                            ImGui::GetStyle().WindowPadding.y * 2.0f);
                // Clamp to reasonable bounds
                if (newW < INITIAL_WIDTH) newW = INITIAL_WIDTH;
                if (newH < 200) newH = 200;
                SDL_SetWindowSize(m_window, newW, newH);
                SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            }
        }
    }

    int displayW = 0;
    int displayH = 0;
    SDL_GL_GetDrawableSize(m_window, &displayW, &displayH);
    glViewport(0, 0, displayW, displayH);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(m_window);
}

void SettingsApp::renderGeneralTab() {
    ImGui::Spacing();

    ImGui::Text("Startup");
    ImGui::Indent();
    if (ImGui::Checkbox("Show Start Page at launch", &m_showStartPage))
        m_dirty = true;
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Viewport");
    ImGui::Indent();
    if (ImGui::Checkbox("Show Grid", &m_showGrid))
        m_dirty = true;
    if (ImGui::Checkbox("Show Axis", &m_showAxis))
        m_dirty = true;
    if (ImGui::Checkbox("Auto-orient models", &m_autoOrient))
        m_dirty = true;
    if (ImGui::Checkbox("Invert orbit X", &m_invertOrbitX))
        m_dirty = true;
    if (ImGui::Checkbox("Invert orbit Y", &m_invertOrbitY))
        m_dirty = true;

    ImGui::Spacing();
    const char* navStyles[] = {"Default", "CAD (SolidWorks)", "Maya"};
    if (ImGui::Combo("Navigation", &m_navStyle, navStyles, 3))
        m_dirty = true;

    // Show hint for selected style
    switch (m_navStyle) {
    case 0:
        ImGui::TextDisabled("Left=Orbit, Shift+Left=Pan, Middle=Pan, Right=Zoom");
        break;
    case 1:
        ImGui::TextDisabled("Middle=Orbit, Shift+Middle=Pan, Right=Pan, Scroll=Zoom");
        break;
    case 2:
        ImGui::TextDisabled("Alt+Left=Orbit, Alt+Middle=Pan, Alt+Right=Zoom");
        break;
    }
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Logging");
    ImGui::Indent();
    const char* logLevels[] = {"Debug", "Info", "Warning", "Error"};
    if (ImGui::Combo("Log Level", &m_logLevel, logLevels, 4))
        m_dirty = true;
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Local AI");
    ImGui::Indent();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("LM Studio Endpoint", m_lmStudioEndpoint, sizeof(m_lmStudioEndpoint)))
        m_dirty = true;
    ImGui::TextDisabled("Default: http://127.0.0.1:1234/v1/chat/completions");
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

#ifdef __linux__
    ImGui::Text("Windows");
    ImGui::Indent();
    if (ImGui::Checkbox("Enable Floating Windows", &m_enableFloatingWindows))
        m_dirty = true;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Allow undocked panels to float as independent OS windows.\n"
                          "Forces X11 mode on Wayland (via XWayland).\n"
                          "Requires application restart.");
    }
    ImGui::TextDisabled("Requires restart. Uses X11/XWayland on Wayland.");
    ImGui::Unindent();
#endif
}

void SettingsApp::renderAppearanceTab() {
    ImGui::Spacing();

    ImGui::Text("Theme");
    ImGui::Indent();
    const char* themes[] = {"Dark", "Light", "High Contrast"};
    if (ImGui::Combo("##Theme", &m_themeIndex, themes, 3)) {
        m_dirty = true;
        applyThemePreview();
    }
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("UI Scale");
    ImGui::Indent();
    if (ImGui::SliderFloat("##Scale", &m_uiScale, 0.75f, 2.0f, "%.2f"))
        m_dirty = true;
    if (ImGui::Button("Reset to 100%%")) {
        m_uiScale = 1.0f;
        m_dirty = true;
    }
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::TextDisabled("Note: Theme changes are applied live.");
    ImGui::TextDisabled("UI Scale changes require a restart of the main application.");
}

void SettingsApp::renderRenderingTab() {
    ImGui::Spacing();

    ImGui::Text("Light Direction");
    ImGui::Indent();
    if (ImGui::SliderFloat("X##LightDir", &m_lightDir.x, -1.0f, 1.0f))
        m_dirty = true;
    if (ImGui::SliderFloat("Y##LightDir", &m_lightDir.y, -1.0f, 1.0f))
        m_dirty = true;
    if (ImGui::SliderFloat("Z##LightDir", &m_lightDir.z, -1.0f, 1.0f))
        m_dirty = true;
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Light Color");
    ImGui::Indent();
    float lc[3] = {m_lightColor.x, m_lightColor.y, m_lightColor.z};
    if (ImGui::ColorEdit3("##LightColor", lc)) {
        m_lightColor = {lc[0], lc[1], lc[2]};
        m_dirty = true;
    }
    ImGui::Unindent();

    ImGui::Spacing();

    ImGui::Text("Ambient Color");
    ImGui::Indent();
    float ac[3] = {m_ambient.x, m_ambient.y, m_ambient.z};
    if (ImGui::ColorEdit3("##AmbientColor", ac)) {
        m_ambient = {ac[0], ac[1], ac[2]};
        m_dirty = true;
    }
    ImGui::Unindent();

    ImGui::Spacing();

    ImGui::Text("Object Color");
    ImGui::Indent();
    if (ImGui::ColorEdit3("##ObjectColor", m_objectColor))
        m_dirty = true;

    // Color presets
    ImGui::Text("Presets:");
    ImGui::SameLine();
    struct Preset {
        const char* name;
        float r, g, b;
    };
    static constexpr Preset presets[] = {
        {"Steel", 0.6f, 0.6f, 0.65f},  {"Copper", 0.72f, 0.45f, 0.20f},
        {"Gold", 0.83f, 0.69f, 0.22f}, {"Blue", 0.4f, 0.6f, 0.8f},
        {"Red", 0.8f, 0.3f, 0.3f},     {"Green", 0.3f, 0.7f, 0.4f},
        {"White", 0.9f, 0.9f, 0.9f},   {"Black", 0.15f, 0.15f, 0.15f},
    };
    for (const auto& p : presets) {
        ImVec4 col(p.r, p.g, p.b, 1.0f);
        if (ImGui::ColorButton(p.name, col, ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20))) {
            m_objectColor[0] = p.r;
            m_objectColor[1] = p.g;
            m_objectColor[2] = p.b;
            m_dirty = true;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::Unindent();

    ImGui::Spacing();

    ImGui::Text("Shininess");
    ImGui::Indent();
    if (ImGui::SliderFloat("##Shininess", &m_shininess, 1.0f, 128.0f, "%.0f"))
        m_dirty = true;
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Presets");
    ImGui::Indent();
    if (ImGui::Button("Default")) {
        m_lightDir = {-0.5f, -1.0f, -0.3f};
        m_lightColor = {1.0f, 1.0f, 1.0f};
        m_ambient = {0.2f, 0.2f, 0.2f};
        m_objectColor[0] = 0.4f;
        m_objectColor[1] = 0.6f;
        m_objectColor[2] = 0.8f;
        m_shininess = 32.0f;
        m_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Warm Studio")) {
        m_lightDir = {-0.3f, -0.8f, -0.5f};
        m_lightColor = {1.0f, 0.95f, 0.85f};
        m_ambient = {0.25f, 0.22f, 0.20f};
        m_shininess = 48.0f;
        m_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cool Workshop")) {
        m_lightDir = {-0.5f, -1.0f, 0.2f};
        m_lightColor = {0.85f, 0.90f, 1.0f};
        m_ambient = {0.18f, 0.20f, 0.25f};
        m_shininess = 24.0f;
        m_dirty = true;
    }
    ImGui::Unindent();
}

void SettingsApp::renderImportTab() {
    ImGui::Spacing();

    // Parallelism section
    ImGui::Text("Parallelism");
    ImGui::Indent();

    const char* parallelismOptions[] = {
        "Auto (60% cores)",
        "Performance (90% cores)",
        "Expert (100% cores)",
    };

    if (ImGui::Combo("##Parallelism", &m_parallelismTier, parallelismOptions, 3)) {
        m_dirty = true;
    }

    // Show actual thread count
    size_t threadCount = calculateThreadCount(static_cast<ParallelismTier>(m_parallelismTier));
    ImGui::TextDisabled("Will use %zu threads on this machine", threadCount);

    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // File Handling section
    ImGui::Text("File Handling");
    ImGui::Indent();

    const char* fileHandlingOptions[] = {
        "Leave in place (default)",
        "Copy to library",
        "Move to library",
    };

    if (ImGui::Combo("##FileHandling", &m_fileHandlingMode, fileHandlingOptions, 3)) {
        m_dirty = true;
    }

    ImGui::TextDisabled("Directory configuration is in the Paths tab.");

    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Notifications section
    ImGui::Text("Notifications");
    ImGui::Indent();

    if (ImGui::Checkbox("Show toast notifications for import errors", &m_showImportErrorToasts)) {
        m_dirty = true;
    }

    ImGui::Unindent();
}

void SettingsApp::renderPathsTab() {
    ImGui::Spacing();

    float labelWidth = 100.0f;
    auto renderDirRow = [this, labelWidth](const char* label, char* buf, size_t bufSize) {
        ImGui::Text("%s", label);
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 50.0f);
        std::string id = std::string("##") + label;
        if (ImGui::InputText(id.c_str(), buf, bufSize))
            m_dirty = true;
        ImGui::SameLine();
        // Validate
        Path p(buf);
        if (!p.empty() && std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
            ImGui::TextColored(colors::kSuccess, "[OK]");
        } else if (p.empty()) {
            ImGui::TextDisabled("[default]");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "[new]");
        }
    };

    ImGui::Text("Workspace Root");
    ImGui::TextDisabled("Set once to relocate all category directories (e.g. network share).");
    ImGui::Spacing();

    ImGui::Indent();
    renderDirRow("Root", m_workspaceRoot, sizeof(m_workspaceRoot));
    ImGui::TextDisabled("Default: %s", paths::getUserRoot().string().c_str());
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Category Directories");
    ImGui::TextDisabled("Override individual directories. Empty = derived from workspace root.");
    ImGui::Spacing();

    ImGui::Indent();
    renderDirRow("Models", m_modelsDir, sizeof(m_modelsDir));
    renderDirRow("Projects", m_projectsDir, sizeof(m_projectsDir));
    renderDirRow("Materials", m_materialsDir, sizeof(m_materialsDir));
    renderDirRow("GCode", m_gcodeDir, sizeof(m_gcodeDir));
    renderDirRow("Support", m_supportDir, sizeof(m_supportDir));
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Internal (read-only)");
    ImGui::Spacing();

    ImGui::Indent();
    ImGui::TextDisabled("Config:");
    ImGui::SameLine(labelWidth);
    ImGui::Text("%s", paths::getConfigDir().string().c_str());

    ImGui::TextDisabled("Database:");
    ImGui::SameLine(labelWidth);
    ImGui::Text("%s", paths::getDatabasePath().string().c_str());

    ImGui::TextDisabled("Cache:");
    ImGui::SameLine(labelWidth);
    ImGui::Text("%s", paths::getCacheDir().string().c_str());
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Open Config Folder")) {
        std::string dir = paths::getConfigDir().string();
        std::system(("xdg-open \"" + dir + "\"").c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Projects Folder")) {
        std::string dir = std::string(m_projectsDir);
        if (dir.empty()) dir = paths::getDefaultProjectsDir().string();
        std::system(("xdg-open \"" + dir + "\"").c_str());
    }
}

void SettingsApp::renderBindingsTab() {
    ImGui::Spacing();

    ImGui::Text("Input Bindings");
    ImGui::TextDisabled("Hold the assigned binding and drag in the viewport.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    for (int i = 0; i < static_cast<int>(BindAction::COUNT); ++i) {
        auto action = static_cast<BindAction>(i);
        if (m_bindingRecorder.renderBindingRow(action, m_bindings[static_cast<size_t>(i)],
                                               m_bindings)) {
            m_dirty = true;
        }
    }

    if (!m_bindingRecorder.conflictMessage.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(colors::kErrorText, "%s",
                           m_bindingRecorder.conflictMessage.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Tip: Use modifier keys (Ctrl, Alt, Shift) with mouse buttons.");
    ImGui::TextDisabled("Avoid bindings that overlap with your navigation style.");
}

void SettingsApp::renderAboutTab() {
    ImGui::Spacing();

    ImGui::Text("Digital Workshop");
    ImGui::TextDisabled("Settings Application");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextWrapped("Digital Workshop is a 3D model management application "
                       "for CNC and 3D printing workflows.");

    ImGui::Spacing();

    ImGui::Text("Libraries:");
    ImGui::BulletText("SDL2 - Window management");
    ImGui::BulletText("Dear ImGui - User interface");
    ImGui::BulletText("OpenGL 3.3 - 3D rendering");
    ImGui::BulletText("SQLite3 - Database");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("Built with C++17");
}

void SettingsApp::applySettings() {
    auto& cfg = Config::instance();

    cfg.setThemeIndex(m_themeIndex);
    cfg.setUiScale(m_uiScale);
    cfg.setShowGrid(m_showGrid);
    cfg.setShowAxis(m_showAxis);
    cfg.setLogLevel(m_logLevel);
    cfg.setShowStartPage(m_showStartPage);
    cfg.setAutoOrient(m_autoOrient);
    cfg.setInvertOrbitX(m_invertOrbitX);
    cfg.setInvertOrbitY(m_invertOrbitY);
    cfg.setNavStyleIndex(m_navStyle);

    cfg.setRenderLightDir(m_lightDir);
    cfg.setRenderLightColor(m_lightColor);
    cfg.setRenderAmbient(m_ambient);
    cfg.setRenderObjectColor(Color{m_objectColor[0], m_objectColor[1], m_objectColor[2], 1.0f});
    cfg.setRenderShininess(m_shininess);

    for (int i = 0; i < static_cast<int>(BindAction::COUNT); ++i) {
        cfg.setBinding(static_cast<BindAction>(i), m_bindings[static_cast<size_t>(i)]);
    }

    // Import settings
    cfg.setParallelismTier(static_cast<ParallelismTier>(m_parallelismTier));
    cfg.setFileHandlingMode(static_cast<FileHandlingMode>(m_fileHandlingMode));
    cfg.setLibraryDir(Path(m_libraryDir));
    cfg.setShowImportErrorToasts(m_showImportErrorToasts);
    cfg.setEnableFloatingWindows(m_enableFloatingWindows);

    // Workspace root (empty string = use getUserRoot())
    cfg.setWorkspaceRoot(Path(m_workspaceRoot));

    // Category directories (empty string = use defaults from workspace root)
    cfg.setModelsDir(Path(m_modelsDir));
    cfg.setProjectsDir(Path(m_projectsDir));
    cfg.setMaterialsDir(Path(m_materialsDir));
    cfg.setGCodeDir(Path(m_gcodeDir));
    cfg.setSupportDir(Path(m_supportDir));

    // Local AI service
    cfg.setLMStudioEndpoint(m_lmStudioEndpoint);

    cfg.save();
    m_dirty = false;

    log::info("Settings", "Configuration saved");
}

void SettingsApp::applyThemePreview() {
    switch (m_themeIndex) {
    case 1:
        Theme::applyLight();
        break;
    case 2:
        Theme::applyHighContrast();
        break;
    default:
        Theme::applyDark();
        break;
    }
}

void SettingsApp::shutdown() {
    if (!m_initialized)
        return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (m_glContext) {
        SDL_GL_DeleteContext(m_glContext);
        m_glContext = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }

    SDL_Quit();
    m_initialized = false;
}

} // namespace dw

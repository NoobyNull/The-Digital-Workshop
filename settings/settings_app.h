#pragma once

#include "../src/core/config/config.h"
#include "../src/core/config/input_binding.h"
#include "../src/core/types.h"
#include "../src/ui/widgets/binding_recorder.h"

struct SDL_Window;

namespace dw {

class SettingsApp {
  public:
    SettingsApp();
    ~SettingsApp();

    SettingsApp(const SettingsApp&) = delete;
    SettingsApp& operator=(const SettingsApp&) = delete;

    bool init();
    int run();

  private:
    void processEvents();
    void render();
    void shutdown();

    // Tabs
    void renderGeneralTab();
    void renderAppearanceTab();
    void renderRenderingTab();
    void renderImportTab();
    void renderPathsTab();
    void renderBindingsTab();
    void renderAboutTab();

    // Apply current settings to config and save
    void applySettings();

    // Apply theme preview in this window
    void applyThemePreview();

    SDL_Window* m_window = nullptr;
    void* m_glContext = nullptr;
    bool m_running = false;
    bool m_initialized = false;
    bool m_dirty = false;

    // Cached settings (edited in UI)
    int m_themeIndex = 0;
    f32 m_uiScale = 1.0f;
    bool m_showGrid = true;
    bool m_showAxis = true;
    bool m_autoOrient = true;
    bool m_invertOrbitX = false;
    bool m_invertOrbitY = false;
    int m_navStyle = 0;
    int m_logLevel = 1;
    bool m_showStartPage = true;
    bool m_enableFloatingWindows = false;

    // Render settings
    Vec3 m_lightDir{-0.5f, -1.0f, -0.3f};
    Vec3 m_lightColor{1.0f, 1.0f, 1.0f};
    Vec3 m_ambient{0.2f, 0.2f, 0.2f};
    float m_objectColor[3]{0.4f, 0.6f, 0.8f};
    f32 m_shininess = 32.0f;

    // Input bindings
    std::array<InputBinding, static_cast<int>(BindAction::COUNT)> m_bindings;
    BindingRecorder m_bindingRecorder;

    // Import settings
    int m_parallelismTier = 0; // 0=Auto, 1=Fixed, 2=Expert
    int m_fileHandlingMode = 0; // 0=LeaveInPlace, 1=Copy, 2=Move
    char m_libraryDir[512]{};
    bool m_showImportErrorToasts = true;
    bool m_libraryDirValid = true; // Validation state for library directory

    // Workspace root
    char m_workspaceRoot[512]{};

    // Category directories
    char m_modelsDir[512]{};
    char m_projectsDir[512]{};
    char m_materialsDir[512]{};
    char m_gcodeDir[512]{};
    char m_supportDir[512]{};

    // Local AI service
    char m_lmStudioEndpoint[256]{};

    // Auto-sizing: render a few frames to let ImGui measure, then resize SDL window
    int m_framesToAutoSize = 3;

    static constexpr int INITIAL_WIDTH = 520;
    static constexpr int INITIAL_HEIGHT = 480;
};

} // namespace dw

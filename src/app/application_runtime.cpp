// Digital Workshop - Application Runtime Loop
// Event processing, per-frame updates, rendering, and quit coordination.

#include "app/application.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <glad/gl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_internal.h>
#include <SDL.h>

#include "app/project_session_integration.h"
#include "app/library_workflow_coordinator.h"
#include "core/cnc/cnc_controller.h"
#include "core/cnc/gamepad_input.h"
#include "core/cnc/serial_port.h"
#include "core/config/config.h"
#include "core/import/background_tagger.h"
#include "core/project/project.h"
#include "core/threading/main_thread_queue.h"
#include "managers/config_manager.h"
#include "managers/file_io_manager.h"
#include "managers/ui_manager.h"
#include "modules/design_library/library_picker_flow.h"
#include "modules/workshop/project_resume.h"
#include "ui/dialogs/message_dialog.h"
#include "ui/panels/viewport_panel.h"
#include "ui/widgets/toast.h"

namespace dw {

int Application::run() {
    if (!m_initialized) {
        std::fprintf(stderr, "Application not initialized\n");
        return 1;
    }
    m_running = true;
    while (m_running) {
        if (m_terminationSignalFlag && *m_terminationSignalFlag != 0) {
            *m_terminationSignalFlag = 0;
            quit();
            if (!m_running)
                break;
        }
        processEvents();
        update();
        render();
    }
    return 0;
}

void Application::quit() {
    if (m_backgroundTagger && m_backgroundTagger->isActive()) {
        m_backgroundTagger->stop();
        m_uiManager->showTaggerShutdownDialog(&m_backgroundTagger->progress());
        return;
    }

    requestProjectClose(workshop::ProjectClosePurpose::ApplicationExit,
                        [this](bool closed) {
        if (closed)
            m_running = false;
    });
}

auto Application::mainThreadQueue() -> MainThreadQueue& {
    return *m_mainThreadQueue;
}

void Application::processEvents() {
    std::vector<std::string> droppedFiles;
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT)
            quit();
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
            event.window.windowID == SDL_GetWindowID(m_window))
            quit();
        // Detect monitor change for DPI scaling
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_MOVED) {
            int newDisplay = SDL_GetWindowDisplayIndex(m_window);
            if (newDisplay != m_displayIndex) {
                m_displayIndex = newDisplay;
                float newDpi = detectDpiScale();
                if (std::abs(newDpi - m_dpiScale) > 0.01f) {
                    m_dpiScale = newDpi;
                    float newScale = m_dpiScale * Config::instance().getUiScale();
                    rebuildFontAtlas(newScale);
                }
            }
        }
        if (event.type == SDL_DROPFILE && event.drop.file != nullptr) {
            droppedFiles.emplace_back(event.drop.file);
            SDL_free(event.drop.file);
        }
    }
    if (!droppedFiles.empty()) {
        const auto picker = m_libraryWorkflow ? m_libraryWorkflow->picker().snapshot()
                                              : design_library::LibraryPickerSnapshot{};
        m_pendingImportLibraryPurpose =
            picker.active ? picker.purpose
                          : design_library::LibraryPickerPurpose::ManageLibrary;
        m_fileIOManager->onFilesDropped(droppedFiles);
    }
}

void Application::update() {
    if (m_mainThreadQueue)
        m_mainThreadQueue->processAll();
    m_fileIOManager->processCompletedImports(m_uiManager->viewportPanel(),
                                             m_uiManager->propertiesPanel(),
                                             m_uiManager->libraryPanel(),
                                             [this](const std::vector<ImportedLibraryItem>& items) {
                                                 handleCompletedLibraryImports(items);
                                             });
    refreshProjectShell();
    // Update simulation in viewport panel each frame
    if (m_uiManager && m_uiManager->viewportPanel())
        m_uiManager->viewportPanel()->updateSimulation(ImGui::GetIO().DeltaTime);

    // Poll gamepad input each frame
    if (m_gamepadInput)
        m_gamepadInput->update(ImGui::GetIO().DeltaTime);

    // Periodic serial port scan — update available ports for menu bar Connect button
    u64 ticks = SDL_GetTicks64();
    if (ticks - m_lastPortScanMs >= 2000) {
        m_lastPortScanMs = ticks;
        auto detailedPorts = listSerialPortsDetailed();

        // Build simple port list for existing UI (Connect dropdown)
        std::vector<std::string> ports;
        for (const auto& p : detailedPorts)
            ports.push_back(p.device);
        m_uiManager->setAvailablePorts(ports);

        // Only toast for devices that look like CNC controllers
        if (m_cncController->isSimulating() && m_lastConnectedPort.empty()) {
            for (const auto& p : detailedPorts) {
                if (!p.likelyCnc)
                    continue;
                std::string desc = p.device;
                if (!p.product.empty())
                    desc += " (" + p.product + ")";
                ToastManager::instance().show(
                    ToastType::Info, "CNC Controller Detected", desc, 5.0f);
                m_lastConnectedPort = "__notified__";
                break; // Only toast once
            }
        }
        if (ports.empty() && m_lastConnectedPort == "__notified__") {
            m_lastConnectedPort.clear();
        }
    }

    m_configManager->poll(ticks);

    // An asynchronous project-start completion leaves the Library visible for
    // one presentation frame so its local ImGui name prompt can close cleanly.
    if (m_deferredLibraryCloseFrames > 0 && --m_deferredLibraryCloseFrames == 0 &&
        m_uiManager) {
        m_uiManager->showLibrary() = false;
    }
}

void Application::render() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Manual dockspace that reserves space for the status bar at the bottom
    auto* viewport = ImGui::GetMainViewport();
    const float statusBarH = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2;
    const float projectBarH = m_uiManager->projectContextBarHeight();

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + projectBarH));
    ImGui::SetNextWindowSize(
        ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - statusBarH - projectBarH));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags dockHostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                                     ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpaceHost", nullptr, dockHostFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspaceId);
    ImGui::End();
    if (m_uiManager->isFirstFrame()) {
        m_uiManager->clearFirstFrame();
        if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr ||
            ImGui::DockBuilderGetNode(dockspaceId)->IsLeafNode())
            m_uiManager->setupDefaultDockLayout(dockspaceId);
    }

    m_uiManager->handleKeyboardShortcuts();
    m_uiManager->renderMenuBar();
    m_uiManager->renderProjectContextBar();
    m_uiManager->renderPanels();
    m_uiManager->renderBackgroundUI(ImGui::GetIO().DeltaTime, &m_loadingState);
    m_uiManager->renderRestartPopup([this]() { m_configManager->relaunchApp(); });
    m_uiManager->renderAboutDialog();

    ImGui::Render();
    int displayW = 0, displayH = 0;
    SDL_GL_GetDrawableSize(m_window, &displayW, &displayH);
    glViewport(0, 0, displayW, displayH);
    auto& bgColor = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    glClearColor(bgColor.x, bgColor.y, bgColor.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#ifdef DW_ENABLE_UX_CAPTURE
    if (m_pendingUxCaptureOutput) {
        m_uxCaptureWriteError.clear();
        m_uxCaptureWriteSucceeded = writeUxCaptureBackBuffer(
            *m_pendingUxCaptureOutput, m_uxCaptureWriteError);
        m_uxCaptureWriteComplete = true;
        m_pendingUxCaptureOutput.reset();
    }
#endif
    SDL_GL_SwapWindow(m_window);

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        SDL_GL_MakeCurrent(m_window, m_glContext);
    }
}

} // namespace dw

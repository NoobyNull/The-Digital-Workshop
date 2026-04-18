// Digital Workshop - Toast Notification System Implementation

#include "ui/widgets/toast.h"

#include <algorithm>

#include <imgui.h>

namespace dw {

ToastManager& ToastManager::instance() {
    static ToastManager s_instance;
    return s_instance;
}

void ToastManager::show(ToastType type,
                        const std::string& title,
                        const std::string& message,
                        float duration) {
    // Rate limiting for error toasts
    if (type == ToastType::Error) {
        m_errorCountThisBatch++;
        if (m_errorCountThisBatch > ERROR_TOAST_LIMIT) {
            if (!m_rateLimitActive) {
                // Show a single summary toast
                m_rateLimitActive = true;
                Toast summaryToast;
                summaryToast.type = ToastType::Warning;
                summaryToast.title = "Multiple Import Errors";
                summaryToast.message = "See import summary for details";
                summaryToast.duration = 5.0f;
                m_toasts.push_back(summaryToast);
            }
            return; // Don't show individual error toasts after limit
        }
    }

    Toast toast;
    toast.type = type;
    toast.title = title;
    toast.message = message;
    toast.duration = duration;
    m_toasts.push_back(toast);

    // Limit visible toasts
    if (static_cast<int>(m_toasts.size()) > MAX_VISIBLE) {
        m_toasts.erase(m_toasts.begin());
    }
}

void ToastManager::render(float deltaTime) {
    // Position toasts in top-right corner, stacked vertically
    auto* viewport = ImGui::GetMainViewport();
    float yOffset = viewport->WorkPos.y + ImGui::GetFrameHeightWithSpacing() * 2; // Below menu bar
    const float xPadding = ImGui::GetStyle().WindowPadding.x;
    const float ySpacing = ImGui::GetStyle().ItemSpacing.y;
    const float toastWidth = std::clamp(viewport->WorkSize.x * 0.25f,
                                        ImGui::GetFontSize() * 15.0f,
                                        ImGui::GetFontSize() * 38.0f);

    // Update and render toasts
    for (auto it = m_toasts.begin(); it != m_toasts.end();) {
        Toast& toast = *it;
        toast.elapsed += deltaTime;

        // Calculate opacity (fade out in last FADE_DURATION seconds)
        float timeRemaining = toast.duration - toast.elapsed;
        if (timeRemaining < FADE_DURATION) {
            toast.opacity = timeRemaining / FADE_DURATION;
        } else {
            toast.opacity = 1.0f;
        }

        // Remove expired toasts
        if (toast.elapsed >= toast.duration) {
            it = m_toasts.erase(it);
            continue;
        }

        // Render toast
        ImVec2 windowPos(viewport->WorkPos.x + viewport->WorkSize.x - toastWidth - xPadding,
                         yOffset);
        ImGui::SetNextWindowPos(windowPos);
        ImGui::SetNextWindowSize(ImVec2(toastWidth, 0));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoSavedSettings;

        // Set window alpha for fade effect
        ImGui::SetNextWindowBgAlpha(toast.opacity * 0.95f);

        // Color based on type
        ImVec4 titleColor;
        switch (toast.type) {
        case ToastType::Error:
            titleColor = ImVec4(1.0f, 0.3f, 0.3f, toast.opacity);
            break;
        case ToastType::Warning:
            titleColor = ImVec4(1.0f, 0.8f, 0.2f, toast.opacity);
            break;
        case ToastType::Success:
            titleColor = ImVec4(0.3f, 1.0f, 0.3f, toast.opacity);
            break;
        case ToastType::Info:
        default:
            titleColor = ImVec4(0.4f, 0.7f, 1.0f, toast.opacity);
            break;
        }

        char windowId[64];
        std::snprintf(windowId, sizeof(windowId), "##Toast%p", (void*)&toast);
        if (ImGui::Begin(windowId, nullptr, flags)) {
            ImGui::PushStyleColor(ImGuiCol_Text, titleColor);
            ImGui::TextWrapped("%s", toast.title.c_str());
            ImGui::PopStyleColor();

            if (!toast.message.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(0.9f, 0.9f, 0.9f, toast.opacity * 0.8f));
                ImGui::TextWrapped("%s", toast.message.c_str());
                ImGui::PopStyleColor();
            }

            yOffset += ImGui::GetWindowHeight() + ySpacing;
        }
        ImGui::End();

        ++it;
    }
}

} // namespace dw

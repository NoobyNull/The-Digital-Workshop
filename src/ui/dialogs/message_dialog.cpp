#include "message_dialog.h"

#include <imgui.h>

#include "../icons.h"
#include "../ui_colors.h"

namespace dw {

MessageDialog::MessageDialog() : Dialog("Message") {}

void MessageDialog::render() {
    if (!m_open) {
        return;
    }

    ImGui::OpenPopup(m_title.c_str());

    const auto* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.3f, 0), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(m_title.c_str(), &m_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Icon based on type
        const char* icon = Icons::Info;
        ImVec4 iconColor = colors::kInfo;

        switch (m_type) {
        case MessageType::Warning:
            icon = Icons::Warning;
            iconColor = colors::kWarning;
            break;
        case MessageType::Error:
            icon = Icons::Error;
            iconColor = colors::kError;
            break;
        case MessageType::Question:
            icon = Icons::Question;
            iconColor = colors::kInfo;
            break;
        default:
            break;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, iconColor);
        ImGui::Text("%s", icon);
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextWrapped("%s", m_message.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float buttonWidth = ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 6;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float contentWidth = ImGui::GetContentRegionAvail().x;

        if (m_type == MessageType::Question) {
            // Yes/No buttons
            ImGui::SetCursorPosX((contentWidth - buttonWidth * 2 - spacing) / 2);
            if (ImGui::Button("Yes", ImVec2(buttonWidth, 0))) {
                m_open = false;
                if (m_callback) {
                    m_callback(DialogResult::Yes);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("No", ImVec2(buttonWidth, 0))) {
                m_open = false;
                if (m_callback) {
                    m_callback(DialogResult::No);
                }
            }
        } else {
            // OK button
            ImGui::SetCursorPosX((contentWidth - buttonWidth) / 2);
            if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
                m_open = false;
                if (m_callback) {
                    m_callback(DialogResult::Ok);
                }
            }
        }

        ImGui::EndPopup();
    }
}

void MessageDialog::show(const std::string& title,
                         const std::string& message,
                         MessageType type,
                         std::function<void(DialogResult)> callback) {
    m_title = title;
    m_message = message;
    m_type = type;
    m_callback = std::move(callback);
    m_open = true;
}

MessageDialog& MessageDialog::instance() {
    static MessageDialog dialog;
    return dialog;
}

void MessageDialog::info(const std::string& title, const std::string& message) {
    instance().show(title, message, MessageType::Info);
}

void MessageDialog::warning(const std::string& title, const std::string& message) {
    instance().show(title, message, MessageType::Warning);
}

void MessageDialog::error(const std::string& title, const std::string& message) {
    instance().show(title, message, MessageType::Error);
}

void MessageDialog::question(const std::string& title,
                             const std::string& message,
                             std::function<void(DialogResult)> callback) {
    instance().show(title, message, MessageType::Question, std::move(callback));
}

void MessageDialog::renderGlobal() {
    instance().render();
}

// ConfirmDialog

ConfirmDialog::ConfirmDialog() : Dialog("Confirm") {}

void ConfirmDialog::render() {
    if (!m_open) {
        return;
    }

    ImGui::OpenPopup(m_title.c_str());

    const auto* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.25f, 0), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(m_title.c_str(), &m_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, colors::kInfo);
        ImGui::Text("%s", Icons::Question);
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextWrapped("%s", m_message.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float buttonWidth = ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 6;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float contentWidth = ImGui::GetContentRegionAvail().x;

        ImGui::SetCursorPosX((contentWidth - buttonWidth * 2 - spacing) / 2);

        if (ImGui::Button("Yes", ImVec2(buttonWidth, 0))) {
            m_open = false;
            if (m_callback) {
                m_callback(true);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(buttonWidth, 0))) {
            m_open = false;
            if (m_callback) {
                m_callback(false);
            }
        }

        ImGui::EndPopup();
    }
}

void ConfirmDialog::show(const std::string& title,
                         const std::string& message,
                         std::function<void(bool)> callback) {
    m_title = title;
    m_message = message;
    m_callback = std::move(callback);
    m_open = true;
}

// SavePromptDialog

SavePromptDialog::SavePromptDialog() : Dialog("Save Changes") {}

void SavePromptDialog::render() {
    if (!m_open)
        return;

    ImGui::OpenPopup(m_title.c_str());

    const auto* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.3f, 0), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(m_title.c_str(), &m_open,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, colors::kWarning);
        ImGui::Text("%s", Icons::Warning);
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextWrapped("%s", m_message.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float padding = ImGui::GetStyle().FramePadding.x * 2.0f;
        float saveW = ImGui::CalcTextSize("Save").x + padding * 2;
        float dontSaveW = ImGui::CalcTextSize("Don't Save").x + padding * 2;
        float cancelW = ImGui::CalcTextSize("Cancel").x + padding * 2;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float totalW = saveW + dontSaveW + cancelW + spacing * 2;
        float contentWidth = ImGui::GetContentRegionAvail().x;

        ImGui::SetCursorPosX((contentWidth - totalW) * 0.5f);

        if (ImGui::Button("Save", ImVec2(saveW, 0))) {
            m_open = false;
            if (m_callback)
                m_callback(DialogResult::Yes);
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(dontSaveW, 0))) {
            m_open = false;
            if (m_callback)
                m_callback(DialogResult::No);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(cancelW, 0))) {
            m_open = false;
            if (m_callback)
                m_callback(DialogResult::Cancel);
        }

        ImGui::EndPopup();
    } else {
        // Popup was closed via X button — treat as Cancel
        if (m_callback)
            m_callback(DialogResult::Cancel);
        m_open = false;
    }
}

void SavePromptDialog::show(const std::string& title,
                            const std::string& message,
                            std::function<void(DialogResult)> callback) {
    m_title = title;
    m_message = message;
    m_callback = std::move(callback);
    m_open = true;
}

SavePromptDialog& SavePromptDialog::instance() {
    static SavePromptDialog dialog;
    return dialog;
}

void SavePromptDialog::renderGlobal() {
    instance().render();
}

void SavePromptDialog::prompt(const std::string& title,
                              const std::string& message,
                              std::function<void(DialogResult)> callback) {
    instance().show(title, message, std::move(callback));
}

} // namespace dw

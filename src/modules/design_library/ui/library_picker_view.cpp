#include "library_picker_view.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include <imgui.h>

#include "ui/ui_colors.h"

namespace dw::design_library {
namespace {

void renderDisabledButton(const char* label,
                          bool enabled,
                          ImVec2 size,
                          const std::function<void()>& action) {
    if (!enabled)
        ImGui::BeginDisabled();
    if (ImGui::Button(label, size) && action)
        action();
    if (!enabled)
        ImGui::EndDisabled();
}

std::string rejectedMessage(const LibraryActionResult& result, const char* fallback) {
    return result.message.empty() ? fallback : result.message;
}

void renderWrappedText(const std::string& text) {
    ImGui::TextWrapped("%s", text.c_str());
}

void renderWrappedText(const std::string& text, const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    renderWrappedText(text);
    ImGui::PopStyleColor();
}

} // namespace

void LibraryPickerView::reset() {
    m_projectName.fill('\0');
    m_modalError.clear();
    m_openNamePrompt = false;
    m_namePromptActive = false;
    m_submitPending = false;
}

void LibraryPickerView::beginProjectNamePrompt(
    const LibraryPickerPresentation& presentation) {
    m_projectName.fill('\0');
    const auto count = std::min(presentation.suggestedProjectName.size(),
                                m_projectName.size() - 1);
    std::memcpy(m_projectName.data(), presentation.suggestedProjectName.data(), count);
    m_modalError.clear();
    m_openNamePrompt = true;
}

void LibraryPickerView::render(const LibraryPickerPresentation& presentation,
                               const LibraryPickerViewCallbacks& callbacks) {
    if (!presentation.visible) {
        if (m_namePromptActive && ImGui::BeginPopupModal(
                                      "Name your project", nullptr,
                                      ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        reset();
        return;
    }

    renderWrappedText(presentation.heading);
    renderWrappedText(presentation.guidance,
                      ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::Spacing();

    renderWrappedText(presentation.selectionText);
    if (!presentation.membershipText.empty())
        renderWrappedText(presentation.membershipText, colors::kInfo);

    std::vector<float> actionWidths{ImGui::CalcTextSize(presentation.previewLabel.c_str()).x};
    if (presentation.primaryVisible)
        actionWidths.push_back(ImGui::CalcTextSize(presentation.primaryLabel.c_str()).x);
    actionWidths.push_back(ImGui::CalcTextSize(presentation.cancelLabel.c_str()).x);
    const auto& style = ImGui::GetStyle();
    const bool inlineActions =
        chooseLibraryPickerActionLayout(ImGui::GetContentRegionAvail().x,
                                        actionWidths,
                                        style.FramePadding.x,
                                        style.ItemSpacing.x) ==
        LibraryPickerActionLayout::Inline;
    const ImVec2 buttonSize = inlineActions ? ImVec2(0.0F, 0.0F) : ImVec2(-1.0F, 0.0F);

    renderDisabledButton(presentation.previewLabel.c_str(),
                         presentation.previewEnabled,
                         buttonSize,
                         [this, &callbacks]() {
                             if (!callbacks.preview)
                                 return;
                             const auto result = callbacks.preview();
                             if (result.status == LibraryActionResultStatus::Rejected)
                                 m_modalError =
                                     rejectedMessage(result, "The preview could not be opened.");
                             else
                                 m_modalError.clear();
                         });

    if (presentation.primaryVisible) {
        if (inlineActions)
            ImGui::SameLine();
        renderDisabledButton(presentation.primaryLabel.c_str(),
                             presentation.primaryEnabled,
                             buttonSize,
                             [this, &presentation, &callbacks]() {
                                 if (presentation.purpose == LibraryPickerPurpose::StartProject) {
                                     beginProjectNamePrompt(presentation);
                                     return;
                                 }
                                 if (!callbacks.primary)
                                     return;
                                 const auto result = callbacks.primary({});
                                 if (result.status == LibraryActionResultStatus::Rejected)
                                     m_modalError = rejectedMessage(
                                         result, "The selected items could not be added.");
                                 else
                                     m_modalError.clear();
                             });
    }

    if (inlineActions)
        ImGui::SameLine();
    renderDisabledButton(presentation.cancelLabel.c_str(),
                         presentation.cancelEnabled,
                         buttonSize,
                         [this, &callbacks]() {
                             if (!callbacks.cancel)
                                 return;
                             const auto result = callbacks.cancel();
                             if (result.status == LibraryActionResultStatus::Rejected)
                                 m_modalError = rejectedMessage(
                                     result, "The previous workspace could not be restored.");
                             else
                                 m_modalError.clear();
                         });

    if (!presentation.statusText.empty()) {
        renderWrappedText(presentation.statusText, colors::kInfo);
    }
    if (!presentation.errorText.empty()) {
        renderWrappedText(presentation.errorText, colors::kErrorText);
    }
    if (!m_namePromptActive && !m_modalError.empty())
        renderWrappedText(m_modalError, colors::kErrorText);

    ImGui::Separator();
    renderProjectNamePrompt(presentation, callbacks);
}

void LibraryPickerView::renderProjectNamePrompt(
    const LibraryPickerPresentation& presentation,
    const LibraryPickerViewCallbacks& callbacks) {
    if (m_openNamePrompt) {
        ImGui::OpenPopup("Name your project");
        m_openNamePrompt = false;
        m_namePromptActive = true;
    }
    if (!m_namePromptActive)
        return;

    if (presentation.actionPending)
        m_submitPending = true;
    if (!presentation.actionPending && !presentation.errorText.empty()) {
        m_submitPending = false;
        m_modalError = presentation.errorText;
    }

    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.32F, 0.0F),
                             ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Name your project", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!ImGui::IsPopupOpen("Name your project")) {
            m_namePromptActive = false;
            m_submitPending = false;
        }
        return;
    }

    ImGui::TextWrapped("Give this project a name you will recognize on Home.");
    ImGui::SetNextItemWidth(-1.0F);
    if (m_submitPending)
        ImGui::BeginDisabled();
    const bool enterPressed = ImGui::InputText("Project name",
                                               m_projectName.data(),
                                               m_projectName.size(),
                                               ImGuiInputTextFlags_EnterReturnsTrue);
    if (m_submitPending)
        ImGui::EndDisabled();
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere(-1);

    if (!m_modalError.empty())
        renderWrappedText(m_modalError, colors::kErrorText);
    if (m_submitPending)
        renderWrappedText("Starting your project...", colors::kInfo);

    const bool hasName = !trimLibraryProjectName(m_projectName.data()).empty();
    if (!hasName || m_submitPending)
        ImGui::BeginDisabled();
    const bool startPressed = ImGui::Button("Start project") || enterPressed;
    if (!hasName || m_submitPending)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (m_submitPending)
        ImGui::BeginDisabled();
    const bool cancelPressed = ImGui::Button("Cancel");
    if (m_submitPending)
        ImGui::EndDisabled();

    if (startPressed && hasName && !m_submitPending) {
        const std::string projectName = trimLibraryProjectName(m_projectName.data());
        const auto result = callbacks.primary
                                ? callbacks.primary(projectName)
                                : LibraryActionResult{LibraryActionResultStatus::Rejected,
                                                      "Project creation is not available."};
        if (result.status == LibraryActionResultStatus::Rejected) {
            m_modalError = rejectedMessage(result, "The project could not be started.");
        } else if (result.status == LibraryActionResultStatus::Pending) {
            m_submitPending = true;
            m_modalError.clear();
        } else {
            m_namePromptActive = false;
            ImGui::CloseCurrentPopup();
        }
    } else if (cancelPressed) {
        m_namePromptActive = false;
        m_modalError.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

} // namespace dw::design_library

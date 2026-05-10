// Digital Workshop - Import Options Dialog Implementation

#include "ui/dialogs/import_options_dialog.h"

#include <algorithm>

#include <imgui.h>

#include "core/import/import_mode_policy.h"
#include "ui/icons.h"
#include "ui/ui_colors.h"

namespace dw {

ImportOptionsDialog::ImportOptionsDialog() : Dialog("Import Options") {}

void ImportOptionsDialog::open(const std::vector<Path>& paths) {
    m_paths = paths;
    m_queueForTagging = false;
    m_detectedLocation = StorageLocation::Unknown;

    // Detect filesystem of first file's parent directory
    if (!paths.empty()) {
        Path target = paths.front();
        if (target.has_parent_path())
            target = target.parent_path();
        auto info = detectFilesystem(target);
        m_detectedLocation = info.location;
    }

    auto initialMode =
        import_mode_policy::initialModeFor(m_detectedLocation,
                                           Config::instance().getFileHandlingMode());
    m_selectedMode = static_cast<int>(initialMode);

    m_open = true;
    m_pendingOpen = true;
}

void ImportOptionsDialog::setOnConfirm(ResultCallback callback) {
    m_onConfirm = std::move(callback);
}

void ImportOptionsDialog::render() {
    if (!m_open)
        return;

    if (m_pendingOpen) {
        ImGui::OpenPopup(m_title.c_str());
        m_pendingOpen = false;
    }

    const auto* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.35f, 0), ImGuiCond_Appearing);

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;

    if (ImGui::BeginPopupModal(m_title.c_str(), &m_open, flags)) {
        // File count header
        ImGui::Text("Importing %zu file(s)", m_paths.size());
        ImGui::Spacing();

        // Network source warning
        if (m_detectedLocation == StorageLocation::Network) {
            ImGui::PushStyleColor(ImGuiCol_Text, colors::kWarning);
            ImGui::Text("%s Source appears to be a network/remote drive "
                        "(NAS, cloud sync, etc.)",
                        Icons::Warning);
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        ImGui::Separator();
        ImGui::Spacing();

        // Radio button section
        ImGui::Text("Where should imported files live?");
        ImGui::Spacing();

        int leaveInPlace = static_cast<int>(FileHandlingMode::LeaveInPlace);
        int copyToLib = static_cast<int>(FileHandlingMode::CopyToLibrary);
        int moveToLib = static_cast<int>(FileHandlingMode::MoveToLibrary);

        ImGui::RadioButton("Keep in original location", &m_selectedMode, leaveInPlace);
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextWrapped("Files stay where they are. Path stored as-is -- may break if "
                           "files move.");
        ImGui::PopStyleColor();
        ImGui::Unindent();
        ImGui::Spacing();

        ImGui::RadioButton("Copy to library", &m_selectedMode, copyToLib);
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextWrapped("Safe copy to local library folder.");
        ImGui::PopStyleColor();
        ImGui::Unindent();
        ImGui::Spacing();

        ImGui::RadioButton("Move to library", &m_selectedMode, moveToLib);
        ImGui::Indent();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextWrapped("Moves files into the local library and removes the originals after "
                           "they are stored.");
        ImGui::PopStyleColor();
        ImGui::Unindent();
        ImGui::Spacing();

        if (m_selectedMode == moveToLib) {
            ImGui::PushStyleColor(ImGuiCol_Text, colors::kWarning);
            ImGui::TextWrapped("%s Originals will be deleted from their current location after "
                               "import succeeds.",
                               Icons::Warning);
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        // Network-specific recommendations
        if (m_detectedLocation == StorageLocation::Network) {
            ImGui::PushStyleColor(ImGuiCol_Text, colors::kWarning);
            ImGui::TextWrapped("Recommended: Copy to library. Network files should be "
                               "copied locally for reliable access.");
            ImGui::PopStyleColor();

            // Extra warning if user selects Move on a network drive
            if (m_selectedMode == moveToLib) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, colors::kErrorText);
                ImGui::TextWrapped("%s Moving files from a network drive is not recommended "
                                   "-- connection drops can cause data loss.",
                                   Icons::Warning);
                ImGui::PopStyleColor();
            }
        }

        // AI tagging checkbox
        ImGui::Spacing();
        bool hasEndpoint = !Config::instance().getLMStudioEndpoint().empty();
        if (!hasEndpoint)
            ImGui::BeginDisabled();
        ImGui::Checkbox("Tag with AI after import", &m_queueForTagging);
        if (!hasEndpoint) {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("(set LM Studio endpoint in Settings)");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Action buttons
        float importDlgBtnW = ImGui::CalcTextSize("Import").x + ImGui::GetStyle().FramePadding.x * 4;
        float cancelDlgBtnW = ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 4;
        float dlgBtnW = std::max(importDlgBtnW, cancelDlgBtnW);
        if (ImGui::Button("Import", ImVec2(dlgBtnW, 0))) {
            auto selectedMode = static_cast<FileHandlingMode>(m_selectedMode);
            Config::instance().setFileHandlingMode(selectedMode);
            Config::instance().save();
            if (m_onConfirm) {
                m_onConfirm(selectedMode, m_queueForTagging, m_paths);
            }
            m_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(dlgBtnW, 0))) {
            m_open = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

} // namespace dw

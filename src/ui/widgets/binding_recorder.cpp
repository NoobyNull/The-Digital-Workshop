#include "binding_recorder.h"

#include <imgui.h>

namespace dw {

bool BindingRecorder::renderBindingRow(
    BindAction action,
    InputBinding& binding,
    const std::array<InputBinding, static_cast<int>(BindAction::COUNT)>& allBindings) {

    bool changed = false;
    ImGui::PushID(static_cast<int>(action));

    if (ImGui::BeginTable("##bindingrow", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.65f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        // Action label
        ImGui::Text("%s", bindActionName(action));

        ImGui::TableNextColumn();

        bool isRecordingThis = recording && recordingAction == action;

        if (isRecordingThis) {
            // Recording mode: show prompt and capture input
            ImGui::SetNextFrameWantCaptureKeyboard(true);
            ImGui::SetNextFrameWantCaptureMouse(true);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
            ImGui::Button(">>> Press key/button <<<", ImVec2(ImGui::GetFontSize() * 15.0f, 0));
            ImGui::PopStyleColor();

            // Try to capture
            InputBinding captured;
            if (captureInput(captured)) {
                // Check for conflicts
                if (checkConflict(action, captured, allBindings)) {
                    conflictMessage = "Conflict: \"" + captured.displayName() +
                                      "\" is already bound to \"" + bindActionName(recordingAction) +
                                      "\"";
                } else {
                    binding = captured;
                    conflictMessage.clear();
                    changed = true;
                }
                recording = false;
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                recording = false;
                conflictMessage.clear();
            }
        } else {
            // Display current binding
            std::string label = "[" + binding.displayName() + "]";
            ImGui::Button(label.c_str(), ImVec2(ImGui::GetFontSize() * 15.0f, 0));

            ImGui::SameLine();
            if (ImGui::Button("+")) {
                recording = true;
                recordingAction = action;
                conflictMessage.clear();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to record a new binding");
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            binding = defaultBinding(action);
            conflictMessage.clear();
            changed = true;
        }

        ImGui::EndTable();
    }

    ImGui::PopID();
    return changed;
}

bool BindingRecorder::captureInput(InputBinding& out) {
    auto& io = ImGui::GetIO();

    // Build current modifier state
    ModFlags mods = Mod_None;
    if (io.KeyShift)
        mods = mods | Mod_Shift;
    if (io.KeyCtrl)
        mods = mods | Mod_Ctrl;
    if (io.KeyAlt)
        mods = mods | Mod_Alt;

    // Check mouse buttons (skip left click on the first frame to avoid capturing
    // the "+" button click itself)
    for (int btn = 0; btn < 5; btn++) {
        if (io.MouseClicked[btn]) {
            // Require at least one modifier for mouse buttons 0-2
            // to avoid stealing basic navigation clicks
            if (btn <= 2 && mods == Mod_None) {
                continue;
            }
            out.modifiers = mods;
            out.type = InputType::MouseButton;
            out.value = btn;
            return true;
        }
    }

    // Check keyboard keys
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
        auto key = static_cast<ImGuiKey>(k);
        // Skip pure modifier keys — they're captured via mods, not as triggers
        if (key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift || key == ImGuiKey_LeftCtrl ||
            key == ImGuiKey_RightCtrl || key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt ||
            key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper) {
            continue;
        }

        if (ImGui::IsKeyPressed(key, false)) {
            out.modifiers = mods;
            out.type = InputType::Key;
            out.value = k;
            return true;
        }
    }

    // Escape cancels recording
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        recording = false;
    }

    return false;
}

bool BindingRecorder::checkConflict(
    BindAction selfAction,
    const InputBinding& proposed,
    const std::array<InputBinding, static_cast<int>(BindAction::COUNT)>& allBindings) {

    for (int i = 0; i < static_cast<int>(BindAction::COUNT); i++) {
        if (static_cast<BindAction>(i) == selfAction) {
            continue;
        }
        if (allBindings[static_cast<size_t>(i)] == proposed) {
            return true;
        }
    }
    return false;
}

} // namespace dw

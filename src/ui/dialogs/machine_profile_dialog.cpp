#include "machine_profile_dialog.h"

#include <cstdio>

#include <imgui.h>

#include "../../core/config/config.h"
#include "../../core/gcode/machine_profile.h"
#include "../../core/gcode/machine_profile_customization.h"
#include "../../core/gcode/machine_rigidity.h"
#include "../icons.h"

namespace dw {

void MachineProfileDialog::open() {
    m_open = true;
    auto& config = Config::instance();
    m_editProfileIndex = config.getActiveMachineProfileIndex();
    m_editProfile = config.getMachineProfiles()[static_cast<size_t>(m_editProfileIndex)];
    m_saveFeedback.clear();
}

void MachineProfileDialog::render() {
    if (!m_open)
        return;

    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.28f, viewport->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Machine Profiles", &m_open)) {
        auto& config = Config::instance();
        const auto& profiles = config.getMachineProfiles();
        int activeIdx = config.getActiveMachineProfileIndex();

        // Active profile selector
        ImGui::Text("%s Active Profile", Icons::Settings);
        ImGui::SetNextItemWidth(-1);
        const char* activePreview = profiles[static_cast<size_t>(activeIdx)].name.c_str();
        if (ImGui::BeginCombo("##ActiveProfile", activePreview)) {
            for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
                bool selected = (i == activeIdx);
                if (ImGui::Selectable(profiles[static_cast<size_t>(i)].name.c_str(), selected)) {
                    config.setActiveMachineProfileIndex(i);
                    m_editProfileIndex = i;
                    m_editProfile = profiles[static_cast<size_t>(i)];
                    config.save();
                    if (m_onChanged)
                        m_onChanged();
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();

        // Editing profile selector
        ImGui::Text("Edit Profile:");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##EditProfile",
                              profiles[static_cast<size_t>(m_editProfileIndex)].name.c_str())) {
            for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
                if (ImGui::Selectable(profiles[static_cast<size_t>(i)].name.c_str(),
                                      i == m_editProfileIndex)) {
                    m_editProfileIndex = i;
                    m_editProfile = profiles[static_cast<size_t>(i)];
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();

        // Name
        char nameBuf[128];
        snprintf(nameBuf, sizeof(nameBuf), "%s", m_editProfile.name.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
            m_editProfile.name = nameBuf;

        ImGui::Spacing();

        // Feed rates
        if (ImGui::CollapsingHeader("Feed Rates (mm/min)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::InputFloat("Max X##feed", &m_editProfile.maxFeedRateX, 100.0f, 500.0f, "%.0f");
            ImGui::InputFloat("Max Y##feed", &m_editProfile.maxFeedRateY, 100.0f, 500.0f, "%.0f");
            ImGui::InputFloat("Max Z##feed", &m_editProfile.maxFeedRateZ, 100.0f, 500.0f, "%.0f");
            ImGui::InputFloat("Rapid", &m_editProfile.rapidRate, 100.0f, 500.0f, "%.0f");
            ImGui::InputFloat("Default Feed", &m_editProfile.defaultFeedRate, 100.0f, 500.0f, "%.0f");
            ImGui::InputFloat("Default Plunge", &m_editProfile.defaultPlungeRate, 10.0f, 50.0f, "%.0f");
            ImGui::InputFloat("Default Stepdown", &m_editProfile.defaultStepdown, 0.1f, 0.5f, "%.2f");
            ImGui::Unindent();
        }

        // Acceleration
        if (ImGui::CollapsingHeader("Acceleration (mm/s²)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::InputFloat("Accel X", &m_editProfile.accelX, 10.0f, 50.0f, "%.0f");
            ImGui::InputFloat("Accel Y", &m_editProfile.accelY, 10.0f, 50.0f, "%.0f");
            ImGui::InputFloat("Accel Z", &m_editProfile.accelZ, 10.0f, 50.0f, "%.0f");
            ImGui::Unindent();
        }

        // Travel limits
        if (ImGui::CollapsingHeader("Max Travel (mm)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::InputFloat("Travel X", &m_editProfile.maxTravelX, 10.0f, 50.0f, "%.0f");
            ImGui::InputFloat("Travel Y", &m_editProfile.maxTravelY, 10.0f, 50.0f, "%.0f");
            ImGui::InputFloat("Travel Z", &m_editProfile.maxTravelZ, 1.0f, 10.0f, "%.0f");
            ImGui::Unindent();
        }

        // Connection
        if (ImGui::CollapsingHeader("Connection")) {
            ImGui::Indent();

            int connType = static_cast<int>(m_editProfile.connectionType);
            const char* connItems[] = {"Auto", "Serial", "TCP"};
            if (ImGui::Combo("Connection Type", &connType, connItems, 3))
                m_editProfile.connectionType = static_cast<gcode::ConnectionType>(connType);

            int fwType = static_cast<int>(m_editProfile.preferredFirmware);
            const char* fwItems[] = {"GRBL", "GrblHAL", "FluidNC"};
            if (ImGui::Combo("Firmware", &fwType, fwItems, 3))
                m_editProfile.preferredFirmware = static_cast<FirmwareType>(fwType);

            ImGui::InputInt("Baud Rate", &m_editProfile.baudRate, 9600, 38400);

            if (m_editProfile.connectionType == gcode::ConnectionType::TCP) {
                char hostBuf[128];
                snprintf(hostBuf, sizeof(hostBuf), "%s", m_editProfile.tcpHost.c_str());
                if (ImGui::InputText("TCP Host", hostBuf, sizeof(hostBuf)))
                    m_editProfile.tcpHost = hostBuf;
                ImGui::InputInt("TCP Port", &m_editProfile.tcpPort, 1, 100);
            }

            ImGui::Unindent();
        }

        // Spindle
        if (ImGui::CollapsingHeader("Spindle")) {
            ImGui::Indent();
            ImGui::InputFloat("Max RPM", &m_editProfile.spindleMaxRPM, 1000.0f, 5000.0f, "%.0f");
            ImGui::InputFloat("Power (W)", &m_editProfile.spindlePower, 50.0f, 200.0f, "%.0f");
            ImGui::Checkbox("Supports Reverse (M4)", &m_editProfile.spindleReverse);
            ImGui::Unindent();
        }

        // Drive System
        if (ImGui::CollapsingHeader("Drive System")) {
            ImGui::Indent();
            int driveType = static_cast<int>(m_editProfile.driveSystem);
            const char* driveItems[] = {
                "Belt", "Acme", "Lead Screw", "Ball Screw", "Custom"};
            const f32 previousFactor = static_cast<f32>(
                gcode::effectiveRigidityFactor(m_editProfile));
            if (ImGui::Combo("Drive Type", &driveType, driveItems, 5)) {
                m_editProfile.driveSystem = static_cast<gcode::DriveSystem>(driveType);
                if (m_editProfile.driveSystem == gcode::DriveSystem::Custom) {
                    m_editProfile.customRigidityFactor = previousFactor;
                }
            }

            if (m_editProfile.driveSystem == gcode::DriveSystem::Custom) {
                f32 rigidityPercent = static_cast<f32>(
                    gcode::normalizeRigidityFactor(m_editProfile.customRigidityFactor) * 100.0);
                if (ImGui::SliderFloat(
                        "Rigidity factor",
                        &rigidityPercent,
                        static_cast<f32>(gcode::kMinimumRigidityFactor * 100.0),
                        static_cast<f32>(gcode::kMaximumRigidityFactor * 100.0),
                        "%.0f%%",
                        ImGuiSliderFlags_AlwaysClamp)) {
                    m_editProfile.customRigidityFactor = rigidityPercent / 100.0f;
                }
                ImGui::TextWrapped(
                    "A conservative multiplier for suggested feed and stepdown, not a "
                    "physical stiffness measurement. 100%% keeps the base suggestion; "
                    "80%% uses 80%%. Lower is gentler for an inexperienced setup.");
            }

            ImGui::TextDisabled(
                "Effective rigidity: %.0f%%",
                gcode::effectiveRigidityFactor(m_editProfile) * 100.0);
            ImGui::Unindent();
        }

        // Home Location
        if (ImGui::CollapsingHeader("Home Location")) {
            ImGui::Indent();
            int homeCorner = static_cast<int>(m_editProfile.homeCorner);
            const char* homeItems[] = {"Bottom-Left", "Bottom-Right", "Top-Left", "Top-Right", "Center"};
            if (ImGui::Combo("Home Corner", &homeCorner, homeItems, 5))
                m_editProfile.homeCorner = static_cast<gcode::HomeCorner>(homeCorner);

            // Show computed home position
            f32 homeX = 0.0f, homeY = 0.0f;
            switch (m_editProfile.homeCorner) {
                case gcode::HomeCorner::BottomLeft:  homeX = 0.0f; homeY = 0.0f; break;
                case gcode::HomeCorner::BottomRight: homeX = m_editProfile.maxTravelX; homeY = 0.0f; break;
                case gcode::HomeCorner::TopLeft:     homeX = 0.0f; homeY = m_editProfile.maxTravelY; break;
                case gcode::HomeCorner::TopRight:
                    homeX = m_editProfile.maxTravelX;
                    homeY = m_editProfile.maxTravelY;
                    break;
                case gcode::HomeCorner::Center:
                    homeX = m_editProfile.maxTravelX * 0.5f;
                    homeY = m_editProfile.maxTravelY * 0.5f;
                    break;
            }
            ImGui::TextDisabled(
                "Home position: (%.0f, %.0f) mm",
                static_cast<double>(homeX),
                static_cast<double>(homeY));
            if (m_editProfile.homeCorner == gcode::HomeCorner::Center)
                ImGui::TextDisabled("Center: homes to corners, then moves to midpoint");
            ImGui::Unindent();
        }

        // Capabilities
        if (ImGui::CollapsingHeader("Capabilities")) {
            ImGui::Indent();
            ImGui::Checkbox("Dust Collection", &m_editProfile.hasDustCollection);
            ImGui::Checkbox("Flood Coolant (M8)", &m_editProfile.hasCoolant);
            ImGui::Checkbox("Mist Coolant (M7)", &m_editProfile.hasMistCoolant);
            ImGui::Checkbox("Probe (G38.x)", &m_editProfile.hasProbe);
            ImGui::Checkbox("Tool Changer", &m_editProfile.hasToolChanger);
            ImGui::Checkbox("Tool Length Offset (G43)", &m_editProfile.hasToolLengthOffset);
            ImGui::Unindent();
        }

        ImGui::Separator();
        ImGui::Spacing();

        const bool editingBuiltIn =
            profiles[static_cast<size_t>(m_editProfileIndex)].builtIn;
        if (editingBuiltIn) {
            ImGui::TextWrapped(
                "Built-in preset: Save creates and activates an editable custom profile "
                "so your changes survive a restart.");
        }
        if (!m_saveFeedback.empty()) {
            ImGui::TextWrapped("%s", m_saveFeedback.c_str());
        }

        // Action buttons
        if (ImGui::Button("Save")) {
            bool activeProfileChanged =
                m_editProfileIndex == config.getActiveMachineProfileIndex();
            if (editingBuiltIn) {
                const auto customProfile = gcode::makeEditableMachineProfileCopy(
                    profiles[static_cast<size_t>(m_editProfileIndex)],
                    m_editProfile,
                    profiles);
                config.addMachineProfile(customProfile);
                m_editProfileIndex =
                    static_cast<int>(config.getMachineProfiles().size()) - 1;
                m_editProfile = customProfile;
                config.setActiveMachineProfileIndex(m_editProfileIndex);
                activeProfileChanged = true;
            } else {
                m_editProfile.builtIn = false;
                config.updateMachineProfile(m_editProfileIndex, m_editProfile);
            }

            if (config.save()) {
                m_saveFeedback = editingBuiltIn
                    ? "Custom profile saved and set active."
                    : "Profile saved.";
            } else {
                m_saveFeedback = "Could not write the profile configuration.";
            }
            if (activeProfileChanged && m_onChanged)
                m_onChanged();
        }
        ImGui::SameLine();
        if (ImGui::Button("New Copy")) {
            const auto copy = gcode::makeEditableMachineProfileCopy(
                profiles[static_cast<size_t>(m_editProfileIndex)],
                m_editProfile,
                profiles);
            config.addMachineProfile(copy);
            m_editProfileIndex = static_cast<int>(config.getMachineProfiles().size()) - 1;
            m_editProfile = copy;
            m_saveFeedback = config.save()
                ? "Editable copy created."
                : "Copy created in memory, but the configuration could not be written.";
        }
        ImGui::SameLine();

        bool isBuiltIn = profiles[static_cast<size_t>(m_editProfileIndex)].builtIn;
        if (isBuiltIn)
            ImGui::BeginDisabled();
        if (ImGui::Button("Delete")) {
            config.removeMachineProfile(m_editProfileIndex);
            m_editProfileIndex = config.getActiveMachineProfileIndex();
            m_editProfile = config.getActiveMachineProfile();
            config.save();
            if (m_onChanged)
                m_onChanged();
        }
        if (isBuiltIn)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Set Active")) {
            config.setActiveMachineProfileIndex(m_editProfileIndex);
            config.save();
            if (m_onChanged)
                m_onChanged();
        }
    }
    ImGui::End();
}

} // namespace dw

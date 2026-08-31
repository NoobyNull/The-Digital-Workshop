#include "cam_placeholder_panel.h"

#include <imgui.h>

namespace dw {

const std::string& CamPlaceholderPanel::statusCopy() const noexcept {
    static const std::string copy =
        "Early CAM workflow: pick a design in the Project Plan, generate "
        "default surfacing G-code, then send it to Run.\nFull operation "
        "editors arrive with the CAM workspace rebuild.";
    return copy;
}

void CamPlaceholderPanel::setEngineStatusProvider(std::function<std::string()> provider) {
    m_engineStatusProvider = std::move(provider);
}

std::string CamPlaceholderPanel::engineStatusLine() const {
    return m_engineStatusProvider ? m_engineStatusProvider() : "Engine not started";
}

void CamPlaceholderPanel::setOnStartEngine(std::function<void()> onStartEngine) {
    m_onStartEngine = std::move(onStartEngine);
}

void CamPlaceholderPanel::setActiveSetupProvider(std::function<std::string()> provider) {
    m_activeSetupProvider = std::move(provider);
}

void CamPlaceholderPanel::setJobStatusProvider(std::function<std::string()> provider) {
    m_jobStatusProvider = std::move(provider);
}

void CamPlaceholderPanel::setMachinesProvider(std::function<MachineList()> provider) {
    m_machinesProvider = std::move(provider);
}

void CamPlaceholderPanel::setOnGenerate(
    std::function<void(const std::string&, const std::string&)> onGenerate) {
    m_onGenerate = std::move(onGenerate);
}

void CamPlaceholderPanel::setRunHandoffReadyProvider(std::function<bool()> provider) {
    m_runHandoffReadyProvider = std::move(provider);
}

void CamPlaceholderPanel::setOnSendToRun(std::function<void()> onSendToRun) {
    m_onSendToRun = std::move(onSendToRun);
}

void CamPlaceholderPanel::render() {
    if (!m_open)
        return;

    if (!ImGui::Begin(m_title.c_str(), &m_open)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("%s", statusCopy().c_str());
    ImGui::Separator();

    const std::string setup = m_activeSetupProvider ? m_activeSetupProvider() : std::string{};
    if (setup.empty()) {
        ImGui::TextDisabled("No carve setup active. Choose a design in the Project Plan.");
    } else {
        ImGui::Text("Setup: %s", setup.c_str());
    }

    ImGui::TextWrapped("%s", engineStatusLine().c_str());
    if (ImGui::Button("Start engine") && m_onStartEngine) {
        m_onStartEngine();
    }

    const MachineList machines = m_machinesProvider ? m_machinesProvider() : MachineList{};
    if (machines.empty()) {
        ImGui::TextDisabled("Machine: %s (engine defaults)", m_selectedMachineId.c_str());
    } else {
        const auto* selected = &m_selectedMachineId;
        std::string preview = *selected;
        for (const auto& [id, name] : machines) {
            if (id == *selected)
                preview = name;
        }
        if (ImGui::BeginCombo("Machine", preview.c_str())) {
            for (const auto& [id, name] : machines) {
                if (ImGui::Selectable(name.c_str(), id == m_selectedMachineId))
                    m_selectedMachineId = id;
            }
            ImGui::EndCombo();
        }
    }

    static constexpr std::pair<const char*, const char*> kOrientations[] = {
        {"auto", "Lay flat (auto)"},
        {"none", "As imported"},
        {"yz", "Swap Y/Z"},
        {"xz", "Swap X/Z"},
    };
    const char* orientationLabel = kOrientations[0].second;
    for (const auto& [id, label] : kOrientations) {
        if (m_selectedOrientation == id)
            orientationLabel = label;
    }
    if (ImGui::BeginCombo("Orientation", orientationLabel)) {
        for (const auto& [id, label] : kOrientations) {
            if (ImGui::Selectable(label, m_selectedOrientation == id))
                m_selectedOrientation = id;
        }
        ImGui::EndCombo();
    }

    ImGui::BeginDisabled(setup.empty());
    if (ImGui::Button("Generate G-code") && m_onGenerate) {
        m_onGenerate(m_selectedMachineId, m_selectedOrientation);
    }
    ImGui::EndDisabled();

    const std::string job = m_jobStatusProvider ? m_jobStatusProvider() : std::string{};
    if (!job.empty())
        ImGui::TextWrapped("%s", job.c_str());

    const bool handoffReady = m_runHandoffReadyProvider && m_runHandoffReadyProvider();
    if (handoffReady && ImGui::Button("Send to Run") && m_onSendToRun) {
        m_onSendToRun();
    }

    ImGui::End();
}

} // namespace dw

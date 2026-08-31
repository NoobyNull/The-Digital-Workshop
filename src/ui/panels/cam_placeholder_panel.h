#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "ui/panels/panel.h"

namespace dw {

// The "direct_carve" window during the CAM rebuild (v0.8.0). Now a thin
// workflow surface: shows the active carve setup from the Project Plan,
// drives the engine, and generates default-surfacing G-code back into the
// project. Parameter-parity editors arrive with the full Phase 5 workspace.
// See .planning/CAM-INTEGRATION-DESIGN.md.
class CamPlaceholderPanel : public Panel {
  public:
    // {id, display name} pairs for the machine/postprocessor choice.
    using MachineList = std::vector<std::pair<std::string, std::string>>;

    CamPlaceholderPanel() : Panel("CAM") {}
    void render() override;
    [[nodiscard]] const std::string& statusCopy() const noexcept;

    void setEngineStatusProvider(std::function<std::string()> provider);
    [[nodiscard]] std::string engineStatusLine() const;
    void setOnStartEngine(std::function<void()> onStartEngine);

    // Active carve setup (empty string = none active).
    void setActiveSetupProvider(std::function<std::string()> provider);
    // Job progress/result line (empty string = no job yet).
    void setJobStatusProvider(std::function<std::string()> provider);
    // Machines known to the engine; empty until first engine contact.
    void setMachinesProvider(std::function<MachineList()> provider);
    // Everything the user chose for a generation run. Empty tool ids mean
    // "pick automatically from the tool library".
    struct GenerateOptions {
        std::string machineId;
        std::string orientation; // "auto" or an engine axisSwap
        std::string roughingToolId;
        std::string finishingToolId;
    };

    // Tools offered by the app's .vtdb library as {id, display name}.
    void setToolChoicesProvider(std::function<MachineList()> provider);
    void setOnGenerate(std::function<void(const GenerateOptions&)> onGenerate);
    // True once a generated G-code project item is ready to hand to Run.
    void setRunHandoffReadyProvider(std::function<bool()> provider);
    void setOnSendToRun(std::function<void()> onSendToRun);

    [[nodiscard]] const std::string& selectedMachineId() const noexcept {
        return m_selectedMachineId;
    }
    [[nodiscard]] const std::string& selectedOrientation() const noexcept {
        return m_selectedOrientation;
    }

  private:
    std::function<std::string()> m_engineStatusProvider;
    std::function<void()> m_onStartEngine;
    std::function<std::string()> m_activeSetupProvider;
    std::function<std::string()> m_jobStatusProvider;
    std::function<MachineList()> m_machinesProvider;
    std::function<MachineList()> m_toolChoicesProvider;
    std::function<void(const GenerateOptions&)> m_onGenerate;
    std::function<bool()> m_runHandoffReadyProvider;
    std::function<void()> m_onSendToRun;
    std::string m_selectedMachineId = "fluidnc";
    std::string m_selectedOrientation = "auto";
    std::string m_selectedRoughingToolId;  // empty = auto
    std::string m_selectedFinishingToolId; // empty = auto
};

} // namespace dw

#include "gcode_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include <imgui.h>

#include "../../core/config/config.h"
#include "../../core/database/gcode_repository.h"
#include "../../core/cnc/cnc_controller.h"
#include "../../core/cnc/preflight_check.h"
#include "../../core/gcode/gcode_modal_scanner.h"
#include "../../core/gcode/gcode_viewport_sampling.h"
#include "../../core/mesh/hash.h"
#include "../../core/paths/path_resolver.h"
#include "../../core/project/gcode_project_context.h"
#include "../../core/project/project.h"
#include "../../core/cnc/serial_port.h"
#include "../../core/utils/file_utils.h"
#include "../dialogs/file_dialog.h"
#include "../icons.h"
#include "../ui_colors.h"
#include "../widgets/toast.h"

namespace dw {

namespace {
struct LongPressButton {
    bool holding = false;
    float holdTime = 0.0f;

    bool render(const char* label, ImVec2 size, float requiredMs, bool enabled) {
        if (!enabled) ImGui::BeginDisabled();
        ImGui::Button(label, size);
        if (!enabled) ImGui::EndDisabled();
        if (!enabled) { holding = false; holdTime = 0.0f; return false; }

        bool isHeld = ImGui::IsItemActive();
        if (isHeld) {
            holdTime += ImGui::GetIO().DeltaTime * 1000.0f;
            float progress = holdTime / requiredMs;
            if (progress > 1.0f) progress = 1.0f;
            ImVec2 rmin = ImGui::GetItemRectMin();
            ImVec2 rmax = ImGui::GetItemRectMax();
            ImVec2 fillMax = {rmin.x + (rmax.x - rmin.x) * progress, rmax.y};
            ImGui::GetWindowDrawList()->AddRectFilled(
                rmin, fillMax, IM_COL32(255, 255, 255, 40), 3.0f);
            holding = true;
        } else {
            if (holding) { holding = false; holdTime = 0.0f; }
        }
        if (holdTime >= requiredMs) {
            holding = false;
            holdTime = 0.0f;
            return true;
        }
        return false;
    }
};

static LongPressButton s_startLongPress;

constexpr std::size_t kMaxVisibleGCodeRows = 250000;
constexpr int kLongRuntimeWarningMinutes = 12 * 60;
constexpr int kLargeProgramWarningLines = 1000000;

std::size_t visibleGCodeRowCount(std::size_t commandCount, std::size_t stride)
{
    if (commandCount == 0) return 0;
    if (stride <= 1) return commandCount;
    std::size_t rows = (commandCount + stride - 1) / stride;
    if ((commandCount - 1) % stride != 0) {
        ++rows;
    }
    return rows;
}

std::size_t commandIndexForVisibleRow(std::size_t row,
                                      std::size_t commandCount,
                                      std::size_t stride)
{
    if (commandCount == 0) return 0;
    if (stride <= 1) return std::min(row, commandCount - 1);

    const std::size_t regularRows = (commandCount + stride - 1) / stride;
    if (row >= regularRows) {
        return commandCount - 1;
    }
    return std::min(row * stride, commandCount - 1);
}

void renderGCodeListingRows(const gcode::Program& program,
                            bool cncConnected,
                            int lastAckedLine,
                            int& scrollToLine)
{
    const std::size_t commandCount = program.commands.size();
    const std::size_t stride =
        gcode::viewportSegmentStride(commandCount, kMaxVisibleGCodeRows);
    const std::size_t visibleRows = visibleGCodeRowCount(commandCount, stride);

    if (stride > 1) {
        ImGui::TextDisabled("Large file preview: showing every %zu lines (%zu of %zu). Sender uses all lines.",
                            stride, visibleRows, commandCount);
        ImGui::Separator();
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(std::min<std::size_t>(
        visibleRows, static_cast<std::size_t>(std::numeric_limits<int>::max()))));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::size_t cmdIndex = commandIndexForVisibleRow(
                static_cast<std::size_t>(row), commandCount, stride);
            const auto& cmd = program.commands[cmdIndex];
            const bool isAcked = cncConnected && lastAckedLine >= 0 &&
                                 cmdIndex <= static_cast<std::size_t>(lastAckedLine);
            if (isAcked) {
                ImGui::TextColored(colors::kSuccess, "%6zu  %s",
                                   cmdIndex + 1, cmd.raw.c_str());
            } else {
                ImGui::Text("%6zu  %s", cmdIndex + 1, cmd.raw.c_str());
            }
        }
    }

    if (scrollToLine >= 0 && scrollToLine < static_cast<int>(commandCount)) {
        const std::size_t targetRow = stride <= 1
            ? static_cast<std::size_t>(scrollToLine)
            : static_cast<std::size_t>(scrollToLine) / stride;
        float lineH = ImGui::GetTextLineHeightWithSpacing();
        ImGui::SetScrollY(static_cast<float>(targetRow) * lineH);
        scrollToLine = -1;
    }
}
} // namespace

GCodePanel::GCodePanel() : Panel("G-code") {
    m_availablePorts = listSerialPorts();
}

void GCodePanel::render() {
    if (!m_open)
        return;

    applyMinSize(24, 12);
    if (ImGui::Begin(m_title.c_str(), &m_open)) {
        // Job completion flash bar
        if (m_jobFlashTimer > 0.0f) {
            m_jobFlashTimer -= ImGui::GetIO().DeltaTime;
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            float width = ImGui::GetContentRegionAvail().x;
            float height = 4.0f;
            float alpha = 0.5f + 0.5f * std::sin(m_jobFlashTimer * 6.0f);
            ImU32 flashColor = ImGui::ColorConvertFloat4ToU32(
                ImVec4(0.2f, 0.8f, 0.2f, alpha));
            ImGui::GetWindowDrawList()->AddRectFilled(
                cursorPos, ImVec2(cursorPos.x + width, cursorPos.y + height),
                flashColor);
            ImGui::Dummy(ImVec2(width, height));
        }

        // Override keyboard shortcuts (only when connected)
        if (m_cnc && m_cncConnected) {
            auto& cfg = Config::instance();
            auto checkOverride = [&](BindAction action) -> bool {
                auto b = cfg.getBinding(action);
                if (!b.isValid() || b.type != InputType::Key)
                    return false;
                bool modsMatch = true;
                if ((b.modifiers & Mod_Ctrl) && !ImGui::GetIO().KeyCtrl) modsMatch = false;
                if ((b.modifiers & Mod_Shift) && !ImGui::GetIO().KeyShift) modsMatch = false;
                if ((b.modifiers & Mod_Alt) && !ImGui::GetIO().KeyAlt) modsMatch = false;
                return modsMatch && ImGui::IsKeyPressed(static_cast<ImGuiKey>(b.value), false);
            };

            if (checkOverride(BindAction::FeedOverridePlus)) {
                m_cnc->setFeedOverride(std::min(200, m_machineStatus.feedOverride + 10));
            }
            if (checkOverride(BindAction::FeedOverrideMinus)) {
                m_cnc->setFeedOverride(std::max(10, m_machineStatus.feedOverride - 10));
            }
            if (checkOverride(BindAction::SpindleOverridePlus)) {
                m_cnc->setSpindleOverride(std::min(200, m_machineStatus.spindleOverride + 10));
            }
            if (checkOverride(BindAction::SpindleOverrideMinus)) {
                m_cnc->setSpindleOverride(std::max(10, m_machineStatus.spindleOverride - 10));
            }
        }

        renderModeTabs();
        renderToolbar();
        renderSearchBar();

        if (m_showJobHistory) {
            renderJobHistory();
        }

        if (hasGCode()) {
            ImGui::Separator();

            // Mode-specific top sections
            if (m_mode == GCodePanelMode::Send) {
                renderConnectionBar();
                if (m_cncConnected) {
                    renderMachineStatus();
                    renderPlaybackControls();
                    renderProgressBar();
                    renderCarveProgress();
                    renderFeedOverride();
                }
            }

            ImGui::Separator();

            // Main content area: stats + G-code text listing
            float availWidth = ImGui::GetContentRegionAvail().x;

            if (availWidth < ImGui::GetFontSize() * 26.0f) {
                float statsH = ImGui::GetContentRegionAvail().y * 0.4f;
                ImGui::BeginChild("Stats", ImVec2(0, statsH), true);
                renderStatistics();
                ImGui::EndChild();

                ImGui::BeginChild("GCodeListing", ImVec2(0, 0), true);
                if (hasGCode()) {
                    renderGCodeListingRows(m_program, m_cncConnected,
                                           m_lastAckedLine, m_scrollToLine);
                }
                ImGui::EndChild();
            } else {
                float statsWidth = availWidth * 0.35f;
                float spacing = ImGui::GetStyle().ItemSpacing.x;

                ImGui::BeginChild("Stats", ImVec2(statsWidth, 0), true);
                renderStatistics();

                // Console below stats in sender mode
                if (m_mode == GCodePanelMode::Send) {
                    ImGui::Separator();
                    renderConsole();
                }
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("GCodeListing", ImVec2(availWidth - statsWidth - spacing, 0), true);
                if (hasGCode()) {
                    renderGCodeListingRows(m_program, m_cncConnected,
                                           m_lastAckedLine, m_scrollToLine);
                }
                ImGui::EndChild();
            }
        } else {
            ImGui::TextDisabled("No G-code loaded");
            ImGui::TextDisabled("Open a G-code file to view");

            if (m_mode == GCodePanelMode::Send) {
                ImGui::Separator();
                renderConnectionBar();
            }
        }
    }
    ImGui::End();
}

void GCodePanel::renderModeTabs() {
    if (ImGui::BeginTabBar("##GCodeModeTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("View")) {
            m_mode = GCodePanelMode::View;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Send")) {
            m_mode = GCodePanelMode::Send;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

bool GCodePanel::loadFile(const std::string& path) {
    auto content = file::readText(path);
    if (!content) {
        ToastManager::instance().show(ToastType::Error,
                                      "File Read Error",
                                      "Could not read G-code file: " + path);
        return false;
    }

    gcode::Parser parser;
    m_program = parser.parse(*content);

    if (!m_program.commands.empty()) {
        m_filePath = path;

        // Add to recent G-code files list
        Config::instance().addRecentGCodeFile(path);
        Config::instance().save();

        gcode::Analyzer analyzer;
        analyzer.setMachineProfile(Config::instance().getActiveMachineProfile());
        m_stats = analyzer.analyze(m_program);

        m_currentGCodeId = -1;
        if (m_gcodeRepo) {
            const Path gcodePath(path);
            if (auto existing = m_gcodeRepo->findByHash(hash::computeFile(gcodePath))) {
                m_currentGCodeId = existing->id;
            } else if (auto byPath = m_gcodeRepo->findByPath(
                           PathResolver::makeStorable(gcodePath, PathCategory::GCode))) {
                m_currentGCodeId = byPath->id;
            } else {
                const auto stem = file::getStem(gcodePath);
                auto matches = m_gcodeRepo->findByName(stem);
                auto exact = std::find_if(matches.begin(), matches.end(),
                                          [&stem](const GCodeRecord& rec) {
                                              return rec.name == stem;
                                          });
                if (exact != matches.end()) {
                    m_currentGCodeId = exact->id;
                } else if (!matches.empty()) {
                    m_currentGCodeId = matches.front().id;
                }
            }
        }

        if (m_onProgramLoaded) m_onProgramLoaded(m_program);

        return true;
    }

    ToastManager::instance().show(ToastType::Warning,
                                  "Empty G-code",
                                  "File contains no valid G-code commands");
    return false;
}

void GCodePanel::clear() {
    m_program = gcode::Program{};
    m_stats = gcode::Statistics{};
    m_filePath.clear();
    m_currentGCodeId = -1;
    m_lastAckedLine = -1;
    m_streamProgress = {};

    if (m_onProgramCleared) m_onProgramCleared();
}

void GCodePanel::renderToolbar() {
    if (ImGui::Button("Open")) {
        if (m_fileDialog) {
            m_fileDialog->showOpen("Open G-code",
                                   FileDialog::gcodeFilters(),
                                   [this](const std::string& path) {
                                       if (!path.empty())
                                           loadFile(path);
                                   });
        }
    }

    ImGui::SameLine();
    renderRecentFiles();

    if (m_jobRepo) {
        ImGui::SameLine();
        if (ImGui::Button(m_showJobHistory ? "Hide History" : "History")) {
            m_showJobHistory = !m_showJobHistory;
            if (m_showJobHistory)
                m_jobHistoryDirty = true;
        }
    }

    if (hasGCode()) {
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            clear();
        }

        if (m_projectManager && m_projectManager->currentProject() && m_currentGCodeId > 0) {
            ImGui::SameLine();
            ImGui::Separator();
            ImGui::SameLine();
            bool alreadyInProject = m_gcodeRepo &&
                m_gcodeRepo->isInProject(m_projectManager->currentProject()->id(), m_currentGCodeId);
            if (alreadyInProject) {
                ImGui::BeginDisabled();
                ImGui::Button("In Project");
                ImGui::EndDisabled();
            } else {
                if (ImGui::Button("Add to Project")) {
                    if (m_gcodeRepo) {
                        int sortOrder = static_cast<int>(
                            m_gcodeRepo->findByProject(
                                m_projectManager->currentProject()->id()).size());
                        m_gcodeRepo->addToProject(
                            m_projectManager->currentProject()->id(), m_currentGCodeId, sortOrder);
                        ToastManager::instance().show(ToastType::Success,
                            "Added", "G-code added to project");
                    }
                }
            }
        }
    }
}

void GCodePanel::renderRecentFiles() {
    auto& cfg = Config::instance();
    const auto& recent = cfg.getRecentGCodeFiles();
    if (recent.empty()) {
        ImGui::BeginDisabled();
        ImGui::Button("Recent");
        ImGui::EndDisabled();
        return;
    }

    if (ImGui::Button("Recent")) {
        ImGui::OpenPopup("RecentGCodePopup");
    }
    if (ImGui::BeginPopup("RecentGCodePopup")) {
        for (const auto& rpath : recent) {
            std::string filename = rpath.string();
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
                filename = filename.substr(lastSlash + 1);

            if (ImGui::MenuItem(filename.c_str())) {
                loadFile(rpath.string());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", rpath.string().c_str());
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Clear Recent")) {
            cfg.clearRecentGCodeFiles();
            cfg.save();
        }
        ImGui::EndPopup();
    }
}

void GCodePanel::renderStatistics() {
    if (!hasGCode())
        return;

    renderMachineProfileSelector();
    ImGui::Separator();

    ImGui::Text("%s Statistics", Icons::Info);
    ImGui::Separator();

    if (ImGui::CollapsingHeader("File", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        std::string filename = m_filePath;
        size_t pos = filename.find_last_of("/\\");
        if (pos != std::string::npos)
            filename = filename.substr(pos + 1);
        ImGui::TextWrapped("File: %s", filename.c_str());
        ImGui::Text("Lines: %d", m_stats.lineCount);
        ImGui::Text("Commands: %d", m_stats.commandCount);
        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Time", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        int hours = static_cast<int>(m_stats.estimatedTime / 60);
        int mins = static_cast<int>(m_stats.estimatedTime) % 60;
        if (hours > 0)
            ImGui::Text("Estimated: %dh %dm", hours, mins);
        else
            ImGui::Text("Estimated: %dm", mins);
        ImGui::Unindent();
    }

    if (m_stats.estimatedTime >= kLongRuntimeWarningMinutes ||
        m_stats.lineCount >= kLargeProgramWarningLines) {
        ImGui::PushStyleColor(ImGuiCol_Text, colors::kWarning);
        ImGui::TextWrapped(
            "Large program warning: this file is long enough that extra density is likely "
            "past the useful quality return. Regenerate with a coarser Direct Carve stepover "
            "such as Fine (8%%), Basic (12%%), or Rough (25%%), or use a larger tip/tool to "
            "reduce runtime and file size.");
        ImGui::PopStyleColor();
    }

    if (ImGui::CollapsingHeader("Distance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        ImGui::Text("Total: %.1f mm", static_cast<double>(m_stats.totalPathLength));
        ImGui::Text("Rapid: %.1f mm", static_cast<double>(m_stats.rapidPathLength));
        ImGui::Text("Cutting: %.1f mm", static_cast<double>(m_stats.cuttingPathLength));
        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Bounds", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        const auto& bMin = m_stats.boundsMin;
        const auto& bMax = m_stats.boundsMax;
        ImGui::Text("X: %.1f - %.1f", static_cast<double>(bMin.x), static_cast<double>(bMax.x));
        ImGui::Text("Y: %.1f - %.1f", static_cast<double>(bMin.y), static_cast<double>(bMax.y));
        ImGui::Text("Z: %.1f - %.1f", static_cast<double>(bMin.z), static_cast<double>(bMax.z));
        double sizeX = static_cast<double>(bMax.x - bMin.x);
        double sizeY = static_cast<double>(bMax.y - bMin.y);
        double sizeZ = static_cast<double>(bMax.z - bMin.z);
        ImGui::Text("Size: %.1f x %.1f x %.1f", sizeX, sizeY, sizeZ);
        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Toolpath", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        ImGui::Text("Segments: %zu", m_program.path.size());
        ImGui::Text("Tool Changes: %d", m_stats.toolChangeCount);
        ImGui::Unindent();
    }
}

// --- Machine profile selector ---

void GCodePanel::renderMachineProfileSelector() {
    auto& config = Config::instance();
    const auto& profiles = config.getMachineProfiles();
    int activeIdx = config.getActiveMachineProfileIndex();

    ImGui::Text("%s Machine", Icons::Settings);
    ImGui::SetNextItemWidth(-1);
    const char* preview = profiles[static_cast<size_t>(activeIdx)].name.c_str();
    if (ImGui::BeginCombo("##MachineProfile", preview)) {
        for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
            bool selected = (i == activeIdx);
            if (ImGui::Selectable(profiles[static_cast<size_t>(i)].name.c_str(), selected)) {
                config.setActiveMachineProfileIndex(i);
                reanalyze();
                config.save();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Edit Machine Profile")) {
        if (m_openMachineProfiles)
            m_openMachineProfiles();
    }
}

void GCodePanel::onMachineProfileChanged() { reanalyze(); }

void GCodePanel::reanalyze() {
    if (m_program.commands.empty())
        return;

    gcode::Analyzer analyzer;
    analyzer.setMachineProfile(Config::instance().getActiveMachineProfile());
    m_stats = analyzer.analyze(m_program);
}

// --- Connection bar (Send mode) ---

void GCodePanel::renderConnectionBar() {
    ImGui::Text("%s Connection", Icons::Plug);

    // Mode selector
    ImGui::SameLine();
    if (ImGui::RadioButton("Serial", m_connMode == ConnMode::Serial))
        m_connMode = ConnMode::Serial;
    ImGui::SameLine();
    if (ImGui::RadioButton("TCP", m_connMode == ConnMode::Tcp))
        m_connMode = ConnMode::Tcp;
    ImGui::SameLine();

    if (m_connMode == ConnMode::Serial) {
        // Refresh button
        if (ImGui::Button(Icons::Refresh)) {
            m_availablePorts = listSerialPorts();
            if (m_selectedPort >= m_availablePorts.size())
                m_selectedPort = 0;
        }
        ImGui::SameLine();

        // Port combo
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("/dev/ttyUSB0000").x + ImGui::GetStyle().FramePadding.x * 2);
        if (m_availablePorts.empty()) {
            ImGui::BeginDisabled();
            if (ImGui::BeginCombo("##Port", "No ports found")) {
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
        } else {
            const char* portPreview = m_availablePorts[m_selectedPort].c_str();
            if (ImGui::BeginCombo("##Port", portPreview)) {
                for (size_t i = 0; i < m_availablePorts.size(); ++i) {
                    bool selected = (i == m_selectedPort);
                    if (ImGui::Selectable(m_availablePorts[i].c_str(), selected))
                        m_selectedPort = i;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        // Baud combo
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("115200").x + ImGui::GetStyle().FramePadding.x * 2);
        char baudLabel[16];
        snprintf(baudLabel, sizeof(baudLabel), "%d", BAUD_RATES[m_selectedBaud]);
        if (ImGui::BeginCombo("##Baud", baudLabel)) {
            for (size_t i = 0; i < static_cast<size_t>(NUM_BAUD_RATES); ++i) {
                char label[16];
                snprintf(label, sizeof(label), "%d", BAUD_RATES[i]);
                bool selected = (i == m_selectedBaud);
                if (ImGui::Selectable(label, selected))
                    m_selectedBaud = i;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else {
        // TCP mode: Host input + Port input
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("/dev/ttyUSB0000").x + ImGui::GetStyle().FramePadding.x * 2);
        ImGui::InputText("##TcpHost", m_tcpHost, sizeof(m_tcpHost));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("00000").x + ImGui::GetStyle().FramePadding.x * 2);
        ImGui::InputInt("##TcpPort", &m_tcpPort, 0, 0);
        if (m_tcpPort < 1)
            m_tcpPort = 1;
        if (m_tcpPort > 65535)
            m_tcpPort = 65535;
    }

    // Connect / Disconnect button
    ImGui::SameLine();
    if (m_cncConnected) {
        if (ImGui::Button("Disconnect")) {
            if (m_cnc)
                m_cnc->disconnect();
        }
    } else {
        bool canConnect = false;
        if (m_connMode == ConnMode::Serial)
            canConnect = m_cnc && !m_availablePorts.empty();
        else
            canConnect = m_cnc && m_tcpHost[0] != '\0';

        if (!canConnect)
            ImGui::BeginDisabled();
        if (ImGui::Button("Connect")) {
            if (m_connMode == ConnMode::Serial) {
                if (!m_cnc->connect(m_availablePorts[m_selectedPort], BAUD_RATES[m_selectedBaud])) {
                    addConsoleLine("Failed to open " + m_availablePorts[m_selectedPort], ConsoleLine::Error);
                } else {
                    addConsoleLine("Connecting to " + m_availablePorts[m_selectedPort] + "...", ConsoleLine::Info);
                }
            } else {
                std::string host(m_tcpHost);
                std::string addr = host + ":" + std::to_string(m_tcpPort);
                if (!m_cnc->connectTcp(host, m_tcpPort)) {
                    addConsoleLine("Failed to connect to " + addr, ConsoleLine::Error);
                } else {
                    addConsoleLine("Connecting to " + addr + "...", ConsoleLine::Info);
                }
            }
        }
        if (!canConnect)
            ImGui::EndDisabled();
    }

    if (m_cncConnected) {
        ImGui::SameLine();
        ImGui::TextColored(colors::kSuccess, "%s", m_cncVersion.c_str());
    }
}

// --- Machine status display (Send mode, connected) ---

void GCodePanel::renderMachineStatus() {
    // State badge with color coding
    const char* stateText = "Unknown";
    ImVec4 stateColor = colors::kDimmed;

    switch (m_machineStatus.state) {
    case MachineState::Idle:
        stateText = "IDLE";
        stateColor = colors::kSuccess;
        break;
    case MachineState::Run:
        stateText = "RUN";
        stateColor = ImVec4(0.2f, 0.6f, 1.0f, 1.0f);
        break;
    case MachineState::Hold:
        stateText = "HOLD";
        stateColor = colors::kWarning;
        break;
    case MachineState::Alarm:
        stateText = "ALARM";
        stateColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
        break;
    case MachineState::Home:
        stateText = "HOME";
        stateColor = ImVec4(0.6f, 0.4f, 1.0f, 1.0f);
        break;
    default: break;
    }

    ImGui::TextColored(stateColor, "%s", stateText);
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 3);

    const auto& mp = m_machineStatus.machinePos;
    const auto& wp = m_machineStatus.workPos;
    ImGui::Text("MPos: %.3f, %.3f, %.3f",
                static_cast<double>(mp.x), static_cast<double>(mp.y), static_cast<double>(mp.z));
    ImGui::Text("WPos: %.3f, %.3f, %.3f",
                static_cast<double>(wp.x), static_cast<double>(wp.y), static_cast<double>(wp.z));
    ImGui::Text("Feed: %.0f mm/min  Spindle: %.0f RPM",
                static_cast<double>(m_machineStatus.feedRate),
                static_cast<double>(m_machineStatus.spindleSpeed));

    // Unlock button for alarm state
    if (m_machineStatus.state == MachineState::Alarm) {
        ImGui::SameLine();
        if (ImGui::Button("Unlock ($X)")) {
            if (m_cnc)
                m_cnc->unlock();
        }
    }
}

// --- Playback controls (Send mode) ---

void GCodePanel::renderPlaybackControls() {
    if (!hasGCode())
        return;

    bool isStreaming = m_cnc && m_cnc->isStreaming();

    if (!isStreaming) {
        // Start button — long-press when safety enabled
        // Door interlock: block start when door is active
        bool doorBlocked = Config::instance().getSafetyDoorInterlockEnabled() &&
                           ((m_machineStatus.inputPins & cnc::PIN_DOOR) != 0 ||
                            m_machineStatus.state == MachineState::Door);
        bool canStart = m_cncConnected &&
                        (m_machineStatus.state == MachineState::Idle) &&
                        !doorBlocked;
        auto& cfg = Config::instance();
        if (cfg.getSafetyLongPressEnabled()) {
            float durationMs = static_cast<float>(cfg.getSafetyLongPressDurationMs());
            if (s_startLongPress.render("Hold to Start", ImVec2(0, 0), durationMs, canStart)) {
                buildSendProgram();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Hold for %.1fs to start streaming", static_cast<double>(durationMs / 1000.0f));
            }
        } else {
            if (!canStart)
                ImGui::BeginDisabled();
            if (ImGui::Button("Start")) {
                buildSendProgram();
            }
            if (!canStart)
                ImGui::EndDisabled();
        }
    } else {
        // Pause / Resume
        if (ImGui::Button("Hold")) {
            if (m_cnc)
                m_cnc->feedHold();
        }
        ImGui::SameLine();
        if (ImGui::Button("Resume")) {
            if (m_cnc)
                m_cnc->cycleStart();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            if (m_cnc) {
                if (Config::instance().getSafetyPauseBeforeResetEnabled()) {
                    m_cnc->feedHold(); // Real-time byte, takes effect immediately
                }
                m_cnc->stopStream();
            }
        }
    }

    ImGui::SameLine();
    // Soft reset (emergency)
    if (ImGui::Button("Reset")) {
        if (m_cnc)
            m_cnc->softReset();
    }
}

// --- Progress bar ---

void GCodePanel::renderProgressBar() {
    if (m_streamProgress.totalLines <= 0)
        return;

    float fraction = static_cast<float>(m_streamProgress.ackedLines) /
                     static_cast<float>(m_streamProgress.totalLines);

    // ETA from ack rate
    float elapsed = m_streamProgress.elapsedSeconds;
    float etaStr = 0.0f;
    if (m_streamProgress.ackedLines > 0 && fraction < 1.0f) {
        float rate = static_cast<float>(m_streamProgress.ackedLines) / elapsed;
        float remaining = static_cast<float>(m_streamProgress.totalLines - m_streamProgress.ackedLines);
        etaStr = remaining / rate;
    }

    char overlay[128];
    int etaMin = static_cast<int>(etaStr / 60.0f);
    int etaSec = static_cast<int>(etaStr) % 60;
    snprintf(overlay, sizeof(overlay), "%d / %d  (%.0f%%)  ETA: %d:%02d",
             m_streamProgress.ackedLines, m_streamProgress.totalLines,
             static_cast<double>(fraction * 100.0f), etaMin, etaSec);

    ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay);

    if (m_streamProgress.errorCount > 0) {
        ImGui::TextColored(colors::kError, "Errors: %d",
                           m_streamProgress.errorCount);
    }
}

// --- Direct Carve streaming ---

void GCodePanel::onCarveStreamStart(int totalLines) {
    m_carveStreamActive = true;
    m_carveCurrentLine = 0;
    m_carveTotalLines = totalLines;
    m_carveElapsedSec = 0.0f;
    m_carveAborted = false;
}

void GCodePanel::onCarveStreamProgress(int currentLine, int totalLines, float elapsedSec) {
    m_carveCurrentLine = currentLine;
    m_carveTotalLines = totalLines;
    m_carveElapsedSec = elapsedSec;
}

void GCodePanel::onCarveStreamComplete() {
    m_carveStreamActive = false;
}

void GCodePanel::onCarveStreamAborted() {
    m_carveStreamActive = false;
    m_carveAborted = true;
}

void GCodePanel::renderCarveProgress() {
    if (!m_carveStreamActive || m_carveTotalLines <= 0) return;

    float fraction = static_cast<float>(m_carveCurrentLine) /
                     static_cast<float>(m_carveTotalLines);

    // ETA from line rate
    float etaSec = 0.0f;
    if (m_carveCurrentLine > 0 && fraction < 1.0f) {
        float rate = static_cast<float>(m_carveCurrentLine) / m_carveElapsedSec;
        float remaining = static_cast<float>(m_carveTotalLines - m_carveCurrentLine);
        etaSec = remaining / rate;
    }

    int etaMin = static_cast<int>(etaSec / 60.0f);
    int etaS = static_cast<int>(etaSec) % 60;
    char overlay[128];
    snprintf(overlay, sizeof(overlay), "Carve: %d / %d  (%.0f%%)  ETA: %d:%02d",
             m_carveCurrentLine, m_carveTotalLines,
             static_cast<double>(fraction * 100.0f), etaMin, etaS);

    ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay);
}

// --- Feed override ---

void GCodePanel::renderFeedOverride() {
    ImGui::Text("Feed Override:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.15f);
    if (ImGui::SliderInt("##FeedOvr", &m_feedOverridePercent, 10, 200, "%d%%")) {
        if (m_cnc)
            m_cnc->setFeedOverride(m_feedOverridePercent);
    }
    ImGui::SameLine();
    if (ImGui::Button("100%")) {
        m_feedOverridePercent = 100;
        if (m_cnc)
            m_cnc->setFeedOverride(100);
    }

    // Rapid override buttons
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 3);
    ImGui::Text("Rapid:");
    ImGui::SameLine();
    if (ImGui::SmallButton("25%")) {
        if (m_cnc) m_cnc->setRapidOverride(25);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("50%")) {
        if (m_cnc) m_cnc->setRapidOverride(50);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("R100%")) {
        if (m_cnc) m_cnc->setRapidOverride(100);
    }
}

// --- Console ---

void GCodePanel::renderConsole() {
    if (!ImGui::CollapsingHeader("Console"))
        return;

    float consoleHeight = ImGui::GetContentRegionAvail().y;
    if (consoleHeight < 50.0f)
        consoleHeight = ImGui::GetTextLineHeightWithSpacing() * 5;

    ImGui::BeginChild("##Console", ImVec2(0, consoleHeight), false);
    for (const auto& line : m_consoleLines) {
        ImVec4 color(0.7f, 0.7f, 0.7f, 1.0f);
        switch (line.type) {
        case ConsoleLine::Sent: color = colors::kDimmed; break;
        case ConsoleLine::Ok: color = colors::kSuccess; break;
        case ConsoleLine::Error: color = colors::kError; break;
        case ConsoleLine::Status: color = ImVec4(0.3f, 0.6f, 1.0f, 1.0f); break;
        case ConsoleLine::Info: color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); break;
        }
        ImGui::TextColored(color, "%s", line.text.c_str());
    }
    if (m_consoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

// --- GRBL event callbacks ---

void GCodePanel::onGrblConnected(bool connected, const std::string& version) {
    m_cncConnected = connected;
    m_cncVersion = version;
    if (connected) {
        addConsoleLine("Connected: " + version, ConsoleLine::Info);
        m_availablePorts = listSerialPorts(); // Refresh
    } else if (m_cncVersion.empty()) {
        // Was never connected — banner detection failed
        addConsoleLine("Connection failed — no compatible controller detected", ConsoleLine::Error);
    } else {
        addConsoleLine("Disconnected", ConsoleLine::Info);
        // Finalize active job as aborted on disconnect
        if (m_jobRepo && m_activeJobId > 0) {
            auto modal = GCodeModalScanner::scanToLine(getRawLines(), m_streamProgress.ackedLines);
            m_jobRepo->finishJob(m_activeJobId, "aborted", m_streamProgress.ackedLines,
                                 m_streamProgress.elapsedSeconds, m_streamProgress.errorCount, modal);
            if (m_projectManager && m_projectManager->currentProject()) {
                (void)m_projectManager->currentOpenItems();
            }
            m_activeJobId = -1;
            m_jobHistoryDirty = true;
        }
    }
}

void GCodePanel::onGrblStatus(const MachineStatus& status) {
    m_machineStatus = status;
}

void GCodePanel::onGrblLineAcked(const LineAck& ack) {
    m_lastAckedLine = ack.lineIndex;
    if (!ack.ok) {
        addConsoleLine("error:" + std::to_string(ack.errorCode) + " " + ack.errorMessage,
                       ConsoleLine::Error);
    }
}

void GCodePanel::onGrblProgress(const StreamProgress& progress) {
    m_streamProgress = progress;

    // Periodic progress save (every 50 acked lines)
    if (m_jobRepo && m_activeJobId > 0 &&
        progress.ackedLines - m_lastProgressSave >= 50) {
        m_jobRepo->updateProgress(m_activeJobId, progress.ackedLines, progress.elapsedSeconds);
        m_lastProgressSave = progress.ackedLines;
    }

    // Check completion
    if (progress.ackedLines >= progress.totalLines && progress.totalLines > 0) {
        addConsoleLine("Stream complete", ConsoleLine::Info);

        // Finalize job record
        if (m_jobRepo && m_activeJobId > 0) {
            auto modal = GCodeModalScanner::scanToLine(getRawLines(), progress.ackedLines);
            m_jobRepo->finishJob(m_activeJobId, "completed", progress.ackedLines,
                                 progress.elapsedSeconds, progress.errorCount, modal);
            if (m_projectManager && m_projectManager->currentProject()) {
                (void)m_projectManager->currentOpenItems();
            }
            m_activeJobId = -1;
            m_jobHistoryDirty = true;
        }

        auto& cfg = Config::instance();
        if (cfg.getJobCompletionNotify()) {
            int totalSec = static_cast<int>(progress.elapsedSeconds);
            int min = totalSec / 60;
            int sec = totalSec % 60;
            char msg[128];
            std::snprintf(msg, sizeof(msg), "G-code streaming finished in %d:%02d", min, sec);
            ToastManager::instance().show(ToastType::Success, "Job Complete", msg);
        }

        if (cfg.getJobCompletionFlash()) {
            m_jobFlashTimer = 3.0f;
        }
    }
}

void GCodePanel::onGrblAlarm(int code, const std::string& desc) {
    addConsoleLine("ALARM:" + std::to_string(code) + " " + desc, ConsoleLine::Error);
    ToastManager::instance().show(ToastType::Error, "Alarm", desc);

    // Finalize active job as aborted
    if (m_jobRepo && m_activeJobId > 0) {
        auto modal = GCodeModalScanner::scanToLine(getRawLines(), m_streamProgress.ackedLines);
        m_jobRepo->finishJob(m_activeJobId, "aborted", m_streamProgress.ackedLines,
                             m_streamProgress.elapsedSeconds, m_streamProgress.errorCount, modal);
        if (m_projectManager && m_projectManager->currentProject()) {
            (void)m_projectManager->currentOpenItems();
        }
        m_activeJobId = -1;
        m_jobHistoryDirty = true;
    }
}

void GCodePanel::onGrblError(const std::string& message) {
    addConsoleLine("[MSG] " + message, ConsoleLine::Info);
}

void GCodePanel::onGrblRawLine(const std::string& line, bool isSent) {
    if (isSent)
        addConsoleLine("> " + line, ConsoleLine::Sent);
    else if (line == "ok")
        addConsoleLine("< ok", ConsoleLine::Ok);
    else if (line.front() == '<')
        {} // Skip status reports in console (too noisy)
    else
        addConsoleLine("< " + line, ConsoleLine::Info);
}

void GCodePanel::addConsoleLine(const std::string& text, ConsoleLine::Type type) {
    m_consoleLines.push_back({text, type});
    while (m_consoleLines.size() > MAX_CONSOLE_LINES)
        m_consoleLines.pop_front();
}

std::vector<std::string> GCodePanel::getRawLines() const {
    std::vector<std::string> lines;
    for (const auto& cmd : m_program.commands) {
        lines.push_back(cmd.raw);
    }
    return lines;
}

void GCodePanel::buildSendProgram() {
    if (!m_cnc || !hasGCode())
        return;

    if (m_projectManager && m_projectManager->currentProject()) {
        auto contextLines = buildGCodeProjectContextLines(
            m_projectManager->currentProject()->name(),
            m_projectManager->currentOpenItems(),
            m_currentGCodeId);
        for (const auto& line : contextLines) {
            addConsoleLine(line, ConsoleLine::Info);
        }
    }

    // Run pre-flight checks before streaming (with soft limit check)
    auto& profile = Config::instance().getActiveMachineProfile();
    Vec3 bmin = m_stats.boundsMin;
    Vec3 bmax = m_stats.boundsMax;
    auto issues = runPreflightChecks(*m_cnc, false, false,
                                     &bmin, &bmax, &profile);
    bool hasErrors = false;
    for (const auto& issue : issues) {
        if (issue.severity == PreflightIssue::Error) {
            hasErrors = true;
            addConsoleLine(
                "PRE-FLIGHT ERROR: " + issue.message,
                ConsoleLine::Error);
        }
    }
    if (hasErrors)
        return;

    // Show warnings but proceed
    for (const auto& issue : issues) {
        if (issue.severity == PreflightIssue::Warning)
            addConsoleLine(
                "WARNING: " + issue.message, ConsoleLine::Info);
    }

    // Extract raw command lines, skip blanks and comments
    std::vector<std::string> lines;
    for (const auto& cmd : m_program.commands) {
        std::string cmdLine = cmd.raw;
        // Strip whitespace
        while (!cmdLine.empty() &&
               (cmdLine.back() == ' ' || cmdLine.back() == '\r'))
            cmdLine.pop_back();
        if (cmdLine.empty() ||
            cmdLine.front() == ';' || cmdLine.front() == '(')
            continue;
        // Strip inline comments
        auto semi = cmdLine.find(';');
        if (semi != std::string::npos)
            cmdLine = cmdLine.substr(0, semi);
        lines.push_back(cmdLine);
    }

    m_lastAckedLine = -1;
    m_streamProgress = {};
    m_lastProgressSave = 0;

    // Record job start in database
    if (m_jobRepo) {
        JobRecord job;
        job.fileName = file::getStem(m_filePath) + "." + file::getExtension(m_filePath);
        job.filePath = PathResolver::makeStorable(Path(m_filePath), PathCategory::GCode).string();
        job.totalLines = static_cast<int>(lines.size());
        auto id = m_jobRepo->insert(job);
        m_activeJobId = id.value_or(-1);
        if (id && m_projectManager && m_projectManager->currentProject()) {
            (void)m_projectManager->currentOpenItems();
        }
        m_jobHistoryDirty = true;
    }

    m_cnc->startStream(lines);
    addConsoleLine(
        "Streaming " + std::to_string(lines.size()) + " lines",
        ConsoleLine::Info);
}

// --- Search/Goto (EXT-12) ---

void GCodePanel::renderSearchBar() {
    if (!hasGCode()) return;

    // Toggle search with Ctrl+F
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
        m_searchActive = !m_searchActive;
    }

    if (!m_searchActive) return;

    ImGui::Separator();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.25f);
    bool searchChanged = ImGui::InputText("##Search", m_searchBuf, sizeof(m_searchBuf),
                                           ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SameLine();
    if (ImGui::SmallButton("Find Next") || searchChanged) {
        findNext();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("000000").x + ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::InputInt("##Goto", &m_gotoLine, 0, 0,
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        gotoLineNumber(m_gotoLine);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to line number");

    ImGui::SameLine();
    if (ImGui::SmallButton("X##CloseSearch")) {
        m_searchActive = false;
        m_searchBuf[0] = '\0';
        m_searchResultLine = -1;
    }

    if (m_searchResultLine >= 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("Match at line %d", m_searchResultLine + 1);
    }
}

void GCodePanel::findNext() {
    if (m_searchBuf[0] == '\0') return;

    int total = static_cast<int>(m_program.commands.size());
    if (total == 0) return;

    std::string needle(m_searchBuf);
    for (auto& c : needle) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    int startLine = m_searchResultLine + 1;

    for (int i = 0; i < total; ++i) {
        int idx = (startLine + i) % total;
        std::string upper = m_program.commands[static_cast<size_t>(idx)].raw;
        for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper.find(needle) != std::string::npos) {
            m_searchResultLine = idx;
            m_scrollToLine = idx;
            return;
        }
    }
    m_searchResultLine = -1;
}

void GCodePanel::gotoLineNumber(int lineNum) {
    int idx = lineNum - 1;
    if (idx >= 0 && idx < static_cast<int>(m_program.commands.size())) {
        m_scrollToLine = idx;
        m_searchResultLine = idx;
    }
}

// --- Job History ---

void GCodePanel::renderJobHistory() {
    if (!m_jobRepo)
        return;

    // Refresh cache when dirty
    if (m_jobHistoryDirty) {
        m_jobHistoryCache = m_jobRepo->findRecent(50);
        m_jobHistoryDirty = false;
    }

    ImGui::Separator();
    ImGui::Text("Job History");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear All")) {
        m_jobRepo->clearAll();
        m_jobHistoryDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh"))
        m_jobHistoryDirty = true;

    if (m_jobHistoryCache.empty()) {
        ImGui::TextDisabled("No job history");
        ImGui::Separator();
        return;
    }

    float historyHeight = std::min(ImGui::GetContentRegionAvail().y * 0.4f, ImGui::GetContentRegionAvail().y * 0.5f);
    if (ImGui::BeginChild("JobHistoryList", ImVec2(0, historyHeight), true)) {
        if (ImGui::BeginTable("Jobs", 6,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
            float colStatus = ImGui::CalcTextSize("Status__").x;
            float colLines = ImGui::CalcTextSize("00000/00000").x;
            float colDur = ImGui::CalcTextSize("00:00:00").x;
            float colDate = ImGui::CalcTextSize("2026-02-28 12:00").x;
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, colStatus);
            ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Lines", ImGuiTableColumnFlags_WidthFixed, colLines);
            ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, colDur);
            ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, colDate);
            ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed, colStatus);
            ImGui::TableHeadersRow();

            i64 removeId = -1;
            for (const auto& job : m_jobHistoryCache) {
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(job.id));

                // Status column with color
                ImGui::TableNextColumn();
                if (job.status == "completed") {
                    ImGui::TextColored(colors::kSuccess, "Done");
                } else if (job.status == "aborted") {
                    ImGui::TextColored(colors::kError, "Abort");
                } else if (job.status == "interrupted") {
                    ImGui::TextColored(colors::kWarning, "Crash");
                } else {
                    ImGui::TextColored(colors::kInfo, "Run");
                }

                // File name
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(job.fileName.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", job.filePath.c_str());

                // Lines (acked/total)
                ImGui::TableNextColumn();
                ImGui::Text("%d/%d", job.lastAckedLine, job.totalLines);

                // Duration
                ImGui::TableNextColumn();
                int totalSec = static_cast<int>(job.elapsedSeconds);
                int min = totalSec / 60;
                int sec = totalSec % 60;
                ImGui::Text("%d:%02d", min, sec);

                // Date
                ImGui::TableNextColumn();
                // Show just date portion of ISO timestamp
                if (job.startedAt.size() >= 16)
                    ImGui::TextUnformatted(job.startedAt.substr(0, 16).c_str());
                else
                    ImGui::TextUnformatted(job.startedAt.c_str());

                // Actions
                ImGui::TableNextColumn();
                if (job.status != "completed" && job.status != "running" &&
                    job.lastAckedLine > 0) {
                    if (ImGui::SmallButton("Resume")) {
                        // Load file and pre-set resume line
                        if (loadFile(job.filePath)) {
                            m_showJobHistory = false;
                            m_mode = GCodePanelMode::Send;
                            addConsoleLine(
                                "Loaded from history — resume from line " +
                                    std::to_string(job.lastAckedLine),
                                ConsoleLine::Info);
                        }
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Load file for resume from line %d", job.lastAckedLine);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X"))
                    removeId = job.id;

                ImGui::PopID();
            }

            ImGui::EndTable();

            if (removeId > 0) {
                m_jobRepo->remove(removeId);
                m_jobHistoryDirty = true;
            }
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
}

} // namespace dw

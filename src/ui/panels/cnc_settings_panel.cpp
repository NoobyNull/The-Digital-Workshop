#include "ui/panels/cnc_settings_panel.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include <imgui.h>

#include "core/cnc/cnc_controller.h"
#include "core/cnc/machine_units.h"
#include "core/config/config.h"
#include "core/paths/app_paths.h"
#include "core/utils/log.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/ui_colors.h"
#include "ui/widgets/edit_buffer.h"

namespace dw {

// Aliases for concise usage within this file
static constexpr auto& kModifiedColor = colors::kWarning;
static constexpr auto& kErrorColor = colors::kError;
static constexpr auto& kOkColor = colors::kSuccess;
static constexpr auto& kDimColor = colors::kDimmed;
static constexpr auto& kAxisX = colors::kAxisX;
static constexpr auto& kAxisY = colors::kAxisY;
static constexpr auto& kAxisZ = colors::kAxisZ;

namespace {

const ImVec4 kChangedFromDefault{0.6f, 0.4f, 1.0f, 1.0f}; // Purple for FluidNC $SC

} // namespace

CncSettingsPanel::CncSettingsPanel() : Panel("Firmware") {
    m_advancedView = Config::instance().getAdvancedSettingsView();
}

void CncSettingsPanel::render() {
    if (!m_open)
        return;

    applyMinSize(22, 10);
    if (!ImGui::Begin(m_title.c_str(), &m_open)) {
        ImGui::End();
        return;
    }

    // Machine profile selector — always available regardless of connection
    renderMachineProfileSection();

    if (!m_connected) {
        ImGui::Spacing();
        ImGui::TextColored(kDimColor, "%s Disconnected",
                           Icons::Unlink);
        ImGui::TextDisabled("Connect a CNC machine to view firmware settings");
        ImGui::End();
        return;
    }

    // Process write queue (GRBL EEPROM-safe sequential writes)
    if (m_writing && m_writeIndex < static_cast<int>(m_writeQueue.size())) {
        m_writeTimer += ImGui::GetIO().DeltaTime;
        if (m_writeTimer >= WRITE_DELAY_SEC) {
            m_writeTimer = 0.0f;
            auto& item = m_writeQueue[static_cast<size_t>(m_writeIndex)];
            applySetting(item.key, item.value);
            ++m_writeIndex;
            if (m_writeIndex >= static_cast<int>(m_writeQueue.size())) {
                m_writing = false;
                m_writeQueue.clear();
                m_writeIndex = 0;
                requestSettings();
            }
        }
    }

    renderToolbar();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("SettingsTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Settings")) {
            m_activeTab = 0;
            renderSettingsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Movement")) {
            m_activeTab = 1;
            renderTuningTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Safety")) {
            m_activeTab = 2;
            renderSafetyTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Raw")) {
            m_activeTab = 3;
            renderRawTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (m_showDiffDialog) {
        renderDiffDialog();
    }

    ImGui::End();
}

// --- Machine profile selector ---

void CncSettingsPanel::renderMachineProfileSection() {
    auto& cfg = Config::instance();
    const auto& profiles = cfg.getMachineProfiles();
    int activeIdx = cfg.getActiveMachineProfileIndex();

    ImGui::Text("%s Machine Profile", Icons::Settings);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x -
                            ImGui::CalcTextSize("Edit").x -
                            ImGui::GetStyle().FramePadding.x * 2.0f -
                            ImGui::GetStyle().ItemSpacing.x);
    const char* preview =
        profiles[static_cast<size_t>(activeIdx)].name.c_str();
    if (ImGui::BeginCombo("##MachineProfile", preview)) {
        for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
            bool selected = (i == activeIdx);
            const auto& p = profiles[static_cast<size_t>(i)];
            if (ImGui::Selectable(p.name.c_str(), selected)) {
                cfg.setActiveMachineProfileIndex(i);
                cfg.save();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Edit")) {
        if (!m_profileDialog.isOpen()) {
            m_profileDialog.open();
        } else {
            m_profileDialog.close();
        }
    }

    m_profileDialog.render();
    ImGui::Separator();
}

// --- Advanced ID display ---

void CncSettingsPanel::renderAdvancedId(const UnifiedSetting& s) {
    if (!m_advancedView) return;

    ImGui::SameLine();
    if (m_firmwareType == FirmwareType::FluidNC && !s.fluidncPath.empty()) {
        ImGui::TextColored(kDimColor, "[$%s]", s.fluidncPath.c_str());
    } else if (s.grblId >= 0) {
        ImGui::TextColored(kDimColor, "[$%d]", s.grblId);
    }
}

// --- Unified rendering helpers ---

void CncSettingsPanel::renderUnifiedBool(const std::string& key, const char* label,
                                          const char* onLabel, const char* offLabel) {
    const auto* s = m_settings.get(key);
    if (!s || s->value.empty()) return;

    float val = 0.0f;
    try { val = std::stof(s->value); } catch (...) {}
    bool bval = (val != 0.0f);
    bool origVal = bval;

    if (m_locked) ImGui::BeginDisabled();
    char cbLabel[128];
    std::snprintf(cbLabel, sizeof(cbLabel), "%s##bool_%s", label, key.c_str());
    ImGui::Checkbox(cbLabel, &bval);
    if (m_locked) ImGui::EndDisabled();

    ImGui::SameLine();
    if (bval) {
        ImGui::TextColored(kOkColor, "(%s)", onLabel);
    } else {
        ImGui::TextColored(kDimColor, "(%s)", offLabel);
    }

    renderAdvancedId(*s);

    if (s->changedFromDefault) {
        ImGui::SameLine();
        ImGui::TextColored(kChangedFromDefault, "*");
    }

    if (bval != origVal) {
        m_settings.set(key, bval ? "1" : "0");
    }
}

void CncSettingsPanel::renderUnifiedBitmask(const std::string& key, const char* label) {
    const auto* s = m_settings.get(key);
    if (!s || s->value.empty()) return;

    int mask = 0;
    try { mask = std::stoi(s->value); } catch (...) {}
    int origMask = mask;
    bool bx = (mask & 1) != 0;
    bool by = (mask & 2) != 0;
    bool bz = (mask & 4) != 0;

    if (m_locked) ImGui::BeginDisabled();
    char tableId[64];
    std::snprintf(tableId, sizeof(tableId), "##bitmask_%s", key.c_str());
    if (ImGui::BeginTable(tableId, 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.4f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", label);
        renderAdvancedId(*s);
        ImGui::TableNextColumn();

        char lbl[64];
        std::snprintf(lbl, sizeof(lbl), "X##bm_%s", key.c_str());
        ImGui::TextColored(kAxisX, "X");
        ImGui::SameLine();
        ImGui::Checkbox(lbl, &bx);

        ImGui::SameLine();
        std::snprintf(lbl, sizeof(lbl), "Y##bm_%s", key.c_str());
        ImGui::TextColored(kAxisY, "Y");
        ImGui::SameLine();
        ImGui::Checkbox(lbl, &by);

        ImGui::SameLine();
        std::snprintf(lbl, sizeof(lbl), "Z##bm_%s", key.c_str());
        ImGui::TextColored(kAxisZ, "Z");
        ImGui::SameLine();
        ImGui::Checkbox(lbl, &bz);

        if (s->changedFromDefault) {
            ImGui::SameLine();
            ImGui::TextColored(kChangedFromDefault, "*");
        }
        ImGui::EndTable();
    }
    if (m_locked) ImGui::EndDisabled();

    mask = (bx ? 1 : 0) | (by ? 2 : 0) | (bz ? 4 : 0);
    if (mask != origMask) {
        m_settings.set(key, std::to_string(mask));
    }
}

void CncSettingsPanel::renderUnifiedNumeric(const std::string& key, const char* label,
                                             const char* units, float width) {
    const auto* s = m_settings.get(key);
    if (!s || s->value.empty()) return;

    auto& eb = m_editBuffers[key];
    if (!eb.active) {
        eb.key = key;
        fillBuffer(eb.buf, s->value);
        eb.active = true;
    }

    if (m_locked) ImGui::BeginDisabled();
    char numTableId[64];
    std::snprintf(numTableId, sizeof(numTableId), "##numeric_%s", key.c_str());
    if (ImGui::BeginTable(numTableId, 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.4f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", label);
        renderAdvancedId(*s);
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(width);
        char inputLabel[64];
        std::snprintf(inputLabel, sizeof(inputLabel), "##num_%s", key.c_str());
        if (ImGui::InputText(inputLabel, eb.buf, sizeof(eb.buf),
                              ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_settings.set(key, eb.buf);
            eb.active = false;
        }
        ImGui::PopItemWidth();

        if (units[0] != '\0') {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", units);
        }

        if (s->modified) {
            ImGui::SameLine();
            ImGui::TextColored(kModifiedColor, "(modified)");
        }

        if (s->changedFromDefault) {
            ImGui::SameLine();
            ImGui::TextColored(kChangedFromDefault, "*");
        }
        ImGui::EndTable();
    }
    if (m_locked) ImGui::EndDisabled();
}

void CncSettingsPanel::renderUnifiedPerAxisGroup(const char* label, const char* units,
                                                  const std::string& keyX,
                                                  const std::string& keyY,
                                                  const std::string& keyZ) {
    ImGui::SeparatorText(label);

    const ImVec4* colors[] = {&kAxisX, &kAxisY, &kAxisZ};
    const char* axes[] = {"X", "Y", "Z"};
    const std::string* keys[] = {&keyX, &keyY, &keyZ};

    if (m_locked) ImGui::BeginDisabled();
    for (int i = 0; i < 3; ++i) {
        const auto* s = m_settings.get(*keys[i]);
        if (!s || s->value.empty()) continue;

        auto& eb = m_editBuffers[*keys[i]];
        if (!eb.active) {
            eb.key = *keys[i];
            fillBuffer(eb.buf, s->value);
            eb.active = true;
        }

        char axTableId[64];
        std::snprintf(axTableId, sizeof(axTableId), "##peraxis_%s", keys[i]->c_str());
        if (ImGui::BeginTable(axTableId, 2, ImGuiTableFlags_None)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(*colors[i], "  %s", axes[i]);
            renderAdvancedId(*s);
            ImGui::TableNextColumn();
            ImGui::PushItemWidth(ImGui::GetFontSize() * 8);
            char lbl[64];
            std::snprintf(lbl, sizeof(lbl), "##ax_%s", keys[i]->c_str());
            if (ImGui::InputText(lbl, eb.buf, sizeof(eb.buf),
                                  ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_settings.set(*keys[i], eb.buf);
                eb.active = false;
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", units);

            if (s->modified) {
                ImGui::SameLine();
                ImGui::TextColored(kModifiedColor, "(modified)");
            }
            if (s->changedFromDefault) {
                ImGui::SameLine();
                ImGui::TextColored(kChangedFromDefault, "*");
            }
            ImGui::EndTable();
        }
    }
    if (m_locked) ImGui::EndDisabled();
}

// --- Toolbar ---

void CncSettingsPanel::renderToolbar() {
    bool canWrite =
        m_connected &&
        !m_locked &&
        m_machineState != MachineState::Run &&
        m_machineState != MachineState::Alarm &&
        !m_writing;

    // Helper: SameLine only if the next button fits, otherwise wrap
    const auto& style = ImGui::GetStyle();
    float availRight = ImGui::GetContentRegionAvail().x;
    auto sameLineIfFits = [&](const char* nextLabel) {
        float nextW = ImGui::CalcTextSize(nextLabel).x + style.FramePadding.x * 2;
        float cursorAfter = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
        if (cursorAfter + style.ItemSpacing.x + nextW < availRight)
            ImGui::SameLine();
    };

    char btnLabel[64];
    std::snprintf(btnLabel, sizeof(btnLabel), "%s Read", Icons::Refresh);
    if (ImGui::Button(btnLabel)) {
        requestSettings();
    }
    if (ImGui::IsItemHovered()) {
        if (m_firmwareType == FirmwareType::FluidNC)
            ImGui::SetTooltip("Query all settings ($S + $SC)");
        else
            ImGui::SetTooltip("Query all GRBL settings ($$)");
    }

    char nextLabel[64];
    std::snprintf(nextLabel, sizeof(nextLabel), "%s Write All", Icons::Save);
    sameLineIfFits(nextLabel);

    ImGui::BeginDisabled(!canWrite);
    std::snprintf(btnLabel, sizeof(btnLabel), "%s Write All", Icons::Save);
    if (ImGui::Button(btnLabel)) {
        writeAllModified();
    }
    if (ImGui::IsItemHovered()) {
        if (m_firmwareType == FirmwareType::FluidNC)
            ImGui::SetTooltip("Write all modified settings to FluidNC (RAM)");
        else
            ImGui::SetTooltip(
                "Write all modified settings to GRBL "
                "(with EEPROM-safe delays)");
    }
    ImGui::EndDisabled();

    // Save to Flash — FluidNC only
    if (m_firmwareType == FirmwareType::FluidNC) {
        std::snprintf(nextLabel, sizeof(nextLabel),
                      "%s Save to Flash", Icons::Save);
        sameLineIfFits(nextLabel);
        ImGui::BeginDisabled(!canWrite);
        if (ImGui::Button(nextLabel)) {
            saveToFlash();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Persist RAM settings to flash\n"
                "($CD=config.yaml). Overwrites config.");
        ImGui::EndDisabled();
    }

    std::snprintf(nextLabel, sizeof(nextLabel), "%s Backup", Icons::Export);
    sameLineIfFits(nextLabel);

    std::snprintf(btnLabel, sizeof(btnLabel), "%s Backup", Icons::Export);
    if (ImGui::Button(btnLabel)) {
        backupToFile();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Export settings to JSON file");

    std::snprintf(nextLabel, sizeof(nextLabel), "%s Restore", Icons::Import);
    sameLineIfFits(nextLabel);

    ImGui::BeginDisabled(m_locked);
    std::snprintf(btnLabel, sizeof(btnLabel), "%s Restore", Icons::Import);
    if (ImGui::Button(btnLabel)) {
        restoreFromFile();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(m_locked ? "Unlock to restore settings" : "Import settings from JSON file");
    ImGui::EndDisabled();

    sameLineIfFits("Export Text");

    if (ImGui::Button("Export Text")) {
        exportPlainText();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Export settings as plain text");

    // Lock toggle — prevents accidental changes
    const char* lockIcon = m_locked ? Icons::Lock : Icons::LockOpen;
    char lockLabel[64];
    std::snprintf(lockLabel, sizeof(lockLabel), "%s##settingsLock", lockIcon);
    sameLineIfFits(lockLabel);
    ImVec4 lockColor = m_locked
        ? ImVec4(0.6f, 0.6f, 0.6f, 1.0f)
        : colors::kWarning;
    ImGui::PushStyleColor(ImGuiCol_Text, lockColor);
    if (ImGui::Button(lockLabel)) {
        m_locked = !m_locked;
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(m_locked ? "Unlock to allow editing firmware settings"
                                   : "Lock to prevent accidental changes");

    // Advanced toggle
    sameLineIfFits("Advanced");
    if (ImGui::Checkbox("Advanced", &m_advancedView)) {
        Config::instance().setAdvancedSettingsView(m_advancedView);
        Config::instance().save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show raw firmware identifiers ($N / $path)");

    if (m_writing) {
        ImGui::SameLine();
        ImGui::TextColored(kModifiedColor, "Writing %d/%d...",
                           m_writeIndex + 1,
                           static_cast<int>(m_writeQueue.size()));
    }

    if (m_machineState == MachineState::Run) {
        ImGui::SameLine();
        ImGui::TextColored(kErrorColor, "Cannot write during streaming");
    } else if (m_machineState == MachineState::Alarm) {
        ImGui::SameLine();
        ImGui::TextColored(kErrorColor, "Alarm active -- unlock first ($X)");
    }

    // Firmware info display
    if (!m_firmwareInfo.empty()) {
        ImGui::TextDisabled("Firmware:");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", m_firmwareInfo.c_str());
    }
}

// --- Settings Tab ---

void CncSettingsPanel::renderSettingsTab() {
    if (m_settings.empty()) {
        ImGui::TextDisabled("No settings loaded. Click 'Read' to query firmware.");
        return;
    }

    ImGui::BeginChild("SettingsScroll", ImVec2(0, 0), ImGuiChildFlags_None);

    // --- Signal Configuration ---
    if (ImGui::CollapsingHeader("Signal Configuration", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        ImGui::Spacing();

        ImGui::TextDisabled("Switch type: NC (normally closed) inverts the signal logic");
        ImGui::Spacing();

        renderUnifiedBool("limit_invert", "Limit switches",
                          "NC (Normally Closed)", "NO (Normally Open)");
        renderUnifiedBool("probe_invert", "Probe pin",
                          "NC (Normally Closed)", "NO (Normally Open)");
        renderUnifiedBool("step_enable_invert", "Step enable", "Inverted", "Normal");

        ImGui::Spacing();
        renderUnifiedBitmask("step_pulse_invert", "Step pulse invert");
        renderUnifiedBitmask("direction_invert", "Direction invert");

        ImGui::Spacing();
        ImGui::Unindent();
    }

    // --- Limits & Homing ---
    if (ImGui::CollapsingHeader("Limits & Homing", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        ImGui::Spacing();

        renderUnifiedBool("soft_limits", "Soft limits", "Enabled", "Disabled");
        renderUnifiedBool("hard_limits", "Hard limits", "Enabled", "Disabled");
        renderUnifiedBool("homing_enable", "Homing cycle", "Enabled", "Disabled");

        ImGui::Spacing();
        renderUnifiedBitmask("homing_dir_invert", "Homing direction invert");

        ImGui::Spacing();
        renderUnifiedNumeric("homing_feed", "Homing feed rate", "mm/min", 100.0f);
        renderUnifiedNumeric("homing_seek", "Homing seek rate", "mm/min", 100.0f);
        renderUnifiedNumeric("homing_debounce", "Homing debounce", "ms", 80.0f);
        renderUnifiedNumeric("homing_pulloff", "Homing pull-off", "mm", 80.0f);

        ImGui::Spacing();
        ImGui::Unindent();
    }

    // --- Spindle ---
    if (ImGui::CollapsingHeader("Spindle", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        ImGui::Spacing();

        renderUnifiedNumeric("max_spindle", "Max spindle speed", "RPM", 100.0f);
        renderUnifiedNumeric("min_spindle", "Min spindle speed", "RPM", 100.0f);
        renderUnifiedBool("laser_mode", "Laser mode", "Enabled", "Disabled");

        ImGui::Spacing();
        ImGui::Unindent();
    }

    // --- Motion ---
    if (ImGui::CollapsingHeader("Motion Parameters")) {
        ImGui::Indent();
        ImGui::Spacing();

        renderUnifiedNumeric("step_pulse_time", "Step pulse time", "us", 80.0f);
        renderUnifiedNumeric("step_idle_delay", "Step idle delay", "ms", 80.0f);
        ImGui::SameLine();
        ImGui::TextDisabled("(255 = always on)");
        renderUnifiedNumeric("junction_deviation", "Junction deviation", "mm", 80.0f);
        renderUnifiedNumeric("arc_tolerance", "Arc tolerance", "mm", 80.0f);
        renderUnifiedBool("report_inches", "Report in inches", "Inches", "Millimeters");

        ImGui::Spacing();
        ImGui::Unindent();
    }

    // --- Status Report ---
    if (ImGui::CollapsingHeader("Status Report")) {
        ImGui::Indent();
        ImGui::Spacing();

        const auto* s10 = m_settings.get("status_report");
        if (s10 && !s10->value.empty()) {
            int mask = 0;
            try { mask = std::stoi(s10->value); } catch (...) {}
            int origMask = mask;
            bool workPos = (mask & 1) != 0;
            bool bufferData = (mask & 2) != 0;

            ImGui::Checkbox("Report work position (WPos)##sr0", &workPos);
            ImGui::SameLine();
            if (!workPos) {
                ImGui::TextColored(kDimColor, "(reports MPos instead)");
            } else {
                ImGui::TextColored(kOkColor, "(WPos active)");
            }
            renderAdvancedId(*s10);

            ImGui::Checkbox("Report buffer state##sr1", &bufferData);

            mask = (workPos ? 1 : 0) | (bufferData ? 2 : 0);
            if (mask != origMask) {
                m_settings.set("status_report", std::to_string(mask));
            }
        }

        ImGui::Spacing();
        ImGui::Unindent();
    }

    // --- Extension settings (unknown/firmware-specific) ---
    bool hasExtension = false;
    for (const auto& [key, s] : m_settings.getAll()) {
        if (s.category == "extension" && !s.value.empty()) {
            hasExtension = true;
            break;
        }
    }

    if (hasExtension && ImGui::CollapsingHeader("Extension Settings")) {
        ImGui::Indent();
        ImGui::Spacing();
        if (m_firmwareType == FirmwareType::FluidNC)
            ImGui::TextDisabled("FluidNC-specific settings not mapped to standard GRBL");
        else
            ImGui::TextDisabled("Settings not in standard GRBL -- shown as raw values");
        ImGui::Spacing();

        for (const auto& [key, s] : m_settings.getAll()) {
            if (s.category != "extension" || s.value.empty()) continue;
            renderUnifiedNumeric(key, s.displayName.c_str(), "", 100.0f);
        }

        ImGui::Spacing();
        ImGui::Unindent();
    }

    // FluidNC changed-from-default legend
    if (m_firmwareType == FirmwareType::FluidNC) {
        ImGui::Spacing();
        ImGui::TextColored(kChangedFromDefault, "*");
        ImGui::SameLine();
        ImGui::TextDisabled("= changed from default (per $SC)");
    }

    ImGui::EndChild();
}

// --- Movement Tab ---

void CncSettingsPanel::renderTuningTab() {
    if (m_settings.empty()) {
        ImGui::TextDisabled("No settings loaded. Click 'Read' to query firmware.");
        return;
    }

    ImGui::BeginChild("TuningScroll", ImVec2(0, 0), ImGuiChildFlags_None);

    ImGui::TextDisabled("Per-axis motion parameters");
    ImGui::Spacing();

    renderUnifiedPerAxisGroup("Steps per mm", "steps/mm",
                               "steps_per_mm_x", "steps_per_mm_y", "steps_per_mm_z");
    renderUnifiedPerAxisGroup("Max Feed Rate", "mm/min",
                               "max_feed_x", "max_feed_y", "max_feed_z");
    renderUnifiedPerAxisGroup("Acceleration", "mm/s\xc2\xb2",
                               "accel_x", "accel_y", "accel_z");
    renderUnifiedPerAxisGroup("Max Travel", "mm",
                               "max_travel_x", "max_travel_y", "max_travel_z");

    ImGui::EndChild();
}

// --- Raw Tab ---

void CncSettingsPanel::renderRawTab() {
    if (m_settings.empty()) {
        ImGui::TextDisabled("No settings loaded. Click 'Read' to query firmware.");
        return;
    }

    bool canWrite =
        m_connected &&
        !m_locked &&
        m_machineState != MachineState::Run &&
        m_machineState != MachineState::Alarm &&
        !m_writing;

    ImGui::TextDisabled("All settings as raw key=value pairs");
    ImGui::Spacing();

    int colCount = m_advancedView ? 6 : 5;
    ImGui::BeginTable("rawSettings", colCount,
                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                      ImVec2(0, 0));
    if (m_advancedView) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("$path/to/key").x);
    }
    ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8);
    ImGui::TableSetupColumn("Units", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("steps/mm").x);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("modified_").x);
    ImGui::TableSetupColumn("##apply", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Apply__").x);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    for (const auto& [key, s] : m_settings.getAll()) {
        if (s.value.empty()) continue;

        ImGui::TableNextRow();

        if (m_advancedView) {
            ImGui::TableNextColumn();
            if (m_firmwareType == FirmwareType::FluidNC && !s.fluidncPath.empty()) {
                ImGui::TextDisabled("$%s", s.fluidncPath.c_str());
            } else if (s.grblId >= 0) {
                ImGui::Text("$%d", s.grblId);
            } else {
                ImGui::TextDisabled("--");
            }
        }

        ImGui::TableNextColumn();
        if (s.modified) {
            ImGui::TextColored(kModifiedColor, "%s", s.displayName.c_str());
        } else {
            ImGui::TextUnformatted(s.displayName.c_str());
        }

        ImGui::TableNextColumn();
        auto& eb = m_editBuffers[key];
        if (!eb.active) {
            eb.key = key;
            fillBuffer(eb.buf, s.value);
            eb.active = true;
        }

        ImGui::PushItemWidth(-1);
        char label[64];
        std::snprintf(label, sizeof(label), "##val_%s", key.c_str());
        if (ImGui::InputText(label, eb.buf, sizeof(eb.buf),
                              ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_settings.set(key, eb.buf);
            eb.active = false;
        }
        ImGui::PopItemWidth();

        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", s.units.c_str());

        ImGui::TableNextColumn();
        if (s.modified) {
            ImGui::TextColored(kModifiedColor, "modified");
        } else if (s.changedFromDefault) {
            ImGui::TextColored(kChangedFromDefault, "changed");
        }

        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!canWrite || !s.modified);
        char applyLabel[64];
        std::snprintf(applyLabel, sizeof(applyLabel), "Apply##%s", key.c_str());
        if (ImGui::SmallButton(applyLabel)) {
            applySetting(key, s.value);
        }
        ImGui::EndDisabled();
    }

    ImGui::EndTable();
}

// --- Diff Dialog ---

void CncSettingsPanel::renderDiffDialog() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.35f, viewport->WorkSize.y * 0.4f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Restore Settings Preview", &m_showDiffDialog)) {
        ImGui::End();
        return;
    }

    if (m_diffEntries.empty()) {
        ImGui::Text("No differences found -- backup matches current settings.");
    } else {
        ImGui::Text("%d setting(s) differ:", static_cast<int>(m_diffEntries.size()));
        ImGui::Spacing();

        ImGui::BeginTable("diff", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg);
        ImGui::TableSetupColumn("Setting");
        ImGui::TableSetupColumn("Current");
        ImGui::TableSetupColumn("Backup");
        ImGui::TableSetupColumn("Description");
        ImGui::TableHeadersRow();

        for (const auto& entry : m_diffEntries) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.key.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.currentValue.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(kModifiedColor, "%s", entry.backupValue.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.displayName.c_str());
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!m_diffEntries.empty()) {
        if (ImGui::Button("Apply All Changes")) {
            m_writeQueue.clear();
            for (const auto& entry : m_diffEntries) {
                m_writeQueue.push_back({entry.key, entry.backupValue});
                m_settings.set(entry.key, entry.backupValue);
            }
            m_writeIndex = 0;
            m_writeTimer = 0.0f;
            m_writing = true;
            m_showDiffDialog = false;
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Cancel")) {
        m_showDiffDialog = false;
    }

    ImGui::End();
}

// --- Safety Tab (unchanged from original) ---

void CncSettingsPanel::renderSafetyTab() {
    ImGui::BeginChild("SafetyScroll", ImVec2(0, 0), ImGuiChildFlags_None);

    auto& cfg = Config::instance();

    // --- Display Units ---
    ImGui::SeparatorText("Display Units");
    bool metric = cfg.getDisplayUnitsMetric();
    if (ImGui::RadioButton("Millimeters (mm)", metric)) {
        cfg.setDisplayUnitsMetric(true); cfg.save();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Inches (in)", !metric)) {
        cfg.setDisplayUnitsMetric(false); cfg.save();
    }
    const auto sendUnits = m_cnc ? m_cnc->sendUnits() : cnc::SendUnits::Millimeters;
    ImGui::TextDisabled("Display-only. Generated carve G-code sends %s (%s).",
                        cnc::unitLabel(sendUnits),
                        cnc::gcodeUnitMode(sendUnits));

    ImGui::Spacing();

    // --- Long-Press Confirmation ---
    ImGui::SeparatorText("Long-Press Confirmation");

    bool longPressEnabled = cfg.getSafetyLongPressEnabled();
    if (ImGui::Checkbox("Enable long-press for Home and Start buttons", &longPressEnabled)) {
        cfg.setSafetyLongPressEnabled(longPressEnabled);
        cfg.save();
    }

    if (longPressEnabled) {
        ImGui::Indent();
        int durationMs = cfg.getSafetyLongPressDurationMs();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
        if (ImGui::SliderInt("Duration##longpress", &durationMs, 250, 3000, "%d ms")) {
            cfg.setSafetyLongPressDurationMs(durationMs);
            cfg.save();
        }
        ImGui::Unindent();
    }

    bool abortLongPress = cfg.getSafetyAbortLongPress();
    if (ImGui::Checkbox("Use long-press for Abort (instead of confirmation dialog)",
                        &abortLongPress)) {
        cfg.setSafetyAbortLongPress(abortLongPress);
        cfg.save();
    }

    ImGui::Spacing();

    // --- Continuous Jog Watchdog ---
    ImGui::SeparatorText("Continuous Jog Watchdog");

    bool deadManEnabled = cfg.getSafetyDeadManEnabled();
    if (ImGui::Checkbox("Enable dead-man watchdog", &deadManEnabled)) {
        cfg.setSafetyDeadManEnabled(deadManEnabled);
        cfg.save();
    }

    if (deadManEnabled) {
        ImGui::Indent();
        int timeoutMs = cfg.getSafetyDeadManTimeoutMs();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
        if (ImGui::SliderInt("Timeout##deadman", &timeoutMs, 200, 5000, "%d ms")) {
            cfg.setSafetyDeadManTimeoutMs(timeoutMs);
            cfg.save();
        }
        ImGui::Unindent();
    }

    ImGui::Spacing();

    // --- Machine Protection ---
    ImGui::SeparatorText("Machine Protection");

    bool doorInterlock = cfg.getSafetyDoorInterlockEnabled();
    if (ImGui::Checkbox("Door interlock (block commands when door is active)",
                        &doorInterlock)) {
        cfg.setSafetyDoorInterlockEnabled(doorInterlock);
        cfg.save();
    }

    bool softLimit = cfg.getSafetySoftLimitCheckEnabled();
    if (ImGui::Checkbox("Soft limit pre-check (compare job bounds vs machine travel)",
                        &softLimit)) {
        cfg.setSafetySoftLimitCheckEnabled(softLimit);
        cfg.save();
    }

    bool pauseBeforeReset = cfg.getSafetyPauseBeforeResetEnabled();
    if (ImGui::Checkbox("Pause before reset (send feed hold before soft reset)",
                        &pauseBeforeReset)) {
        cfg.setSafetyPauseBeforeResetEnabled(pauseBeforeReset);
        cfg.save();
    }

    ImGui::Spacing();

    // --- Logging ---
    ImGui::SeparatorText("Logging");

    bool logToFile = cfg.getLogToFile();
    if (ImGui::Checkbox("Log to file", &logToFile)) {
        cfg.setLogToFile(logToFile);
        if (logToFile) {
            Path logPath = cfg.getLogFilePath();
            if (logPath.empty()) {
                logPath = paths::getDataDir() / "digital_workshop.log";
                cfg.setLogFilePath(logPath);
            }
            log::setLogFile(logPath.string());
        } else {
            log::closeLogFile();
        }
        cfg.save();
    }
    if (logToFile) {
        Path logPath = cfg.getLogFilePath();
        ImGui::TextDisabled("Path: %s", logPath.string().c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Changes take effect immediately");

    ImGui::EndChild();
}

// --- Actions ---

void CncSettingsPanel::requestSettings() {
    if (!m_cnc || !m_connected) return;
    m_collecting = true;
    m_collectingSC = false;
    m_settings.clear();
    m_editBuffers.clear();

    if (m_firmwareType == FirmwareType::FluidNC) {
        m_cnc->sendCommand("$S");
    } else {
        m_cnc->sendCommand("$$");
    }
}

void CncSettingsPanel::applySetting(const std::string& key, const std::string& value) {
    if (!m_cnc || !m_connected) return;
    std::string cmd = m_settings.buildSetCommand(key, value, m_firmwareType);
    if (cmd.empty()) return;
    m_cnc->sendCommand(cmd);
}

void CncSettingsPanel::writeAllModified() {
    auto modified = m_settings.getModified();
    if (modified.empty()) return;

    m_writeQueue.clear();
    for (const auto* s : modified) {
        m_writeQueue.push_back({s->key, s->value});
    }

    if (m_firmwareType == FirmwareType::FluidNC) {
        // FluidNC writes are instant (RAM), no EEPROM delay needed
        for (auto& item : m_writeQueue) {
            applySetting(item.key, item.value);
        }
        m_writeQueue.clear();
        requestSettings();
    } else {
        // GRBL: sequential EEPROM-safe writes
        m_writeIndex = 0;
        m_writeTimer = 0.0f;
        m_writing = true;
    }
}

void CncSettingsPanel::saveToFlash() {
    if (!m_cnc || !m_connected || m_firmwareType != FirmwareType::FluidNC) return;
    m_cnc->sendCommand("$CD=config.yaml");
    log::info("CNC", "FluidNC: persisting settings to flash ($CD=config.yaml)");
}

void CncSettingsPanel::backupToFile() {
    if (!m_fileDialog || m_settings.empty()) return;
    std::string json = m_settings.toJsonString();
    m_fileDialog->showSave(
        "Backup Settings",
        {{"JSON Files", "*.json"}},
        "settings_backup.json",
        [json](const std::string& path) {
            std::ofstream ofs(path);
            if (ofs.is_open()) {
                ofs << json;
            }
        });
}

void CncSettingsPanel::restoreFromFile() {
    if (!m_fileDialog) return;
    m_fileDialog->showOpen(
        "Restore Settings",
        {{"JSON Files", "*.json"}},
        [this](const std::string& path) {
            std::ifstream ifs(path);
            if (!ifs.is_open()) return;
            std::ostringstream contentStream;
            contentStream << ifs.rdbuf();
            std::string content = contentStream.str();
            UnifiedSettingsMap backup;
            if (!backup.fromJsonString(content)) return;

            m_restoreSettings = backup;
            m_diffEntries = m_settings.diff(backup);
            m_showDiffDialog = true;
        });
}

void CncSettingsPanel::exportPlainText() {
    if (!m_fileDialog || m_settings.empty()) return;
    std::string content = m_settings.exportPlainText(m_firmwareType);
    std::string defaultName = (m_firmwareType == FirmwareType::FluidNC)
                                  ? "fluidnc_settings.txt"
                                  : "grbl_settings.txt";
    m_fileDialog->showSave(
        "Export Settings",
        {{"Text Files", "*.txt"}},
        defaultName,
        [content](const std::string& path) {
            std::ofstream ofs(path);
            if (ofs.is_open()) {
                ofs << content;
            }
        });
}

// --- Callbacks ---

void CncSettingsPanel::onConnectionChanged(bool connected,
                                            const std::string& /*version*/) {
    m_connected = connected;
    if (connected) {
        if (m_cnc) {
            m_firmwareType = m_cnc->firmwareType();
        }
        requestSettings();
        // Query firmware info ($I)
        if (m_cnc) {
            m_cnc->sendCommand("$I");
            m_requestingInfo = true;
            m_firmwareInfo.clear();
        }
    } else {
        m_collecting = false;
        m_collectingSC = false;
        m_writing = false;
        m_writeQueue.clear();
    }
}

void CncSettingsPanel::onRawLine(const std::string& line, bool isSent) {
    if (isSent) return;

    // Capture $I firmware info response
    if (m_requestingInfo) {
        if (line == "ok") {
            m_requestingInfo = false;
        } else if (line.substr(0, 5) != "error") {
            if (!m_firmwareInfo.empty()) m_firmwareInfo += " | ";
            m_firmwareInfo += line;
        } else {
            m_requestingInfo = false;
        }
    }

    // Parse setting lines from $$ or $S response
    if (!line.empty() && line[0] == '$' && line.find('=') != std::string::npos) {
        if (m_collectingSC) {
            // $SC response: mark changed-from-default
            m_settings.markChangedFromDefault(line);
        } else if (m_firmwareType == FirmwareType::FluidNC) {
            m_settings.parseFluidNCLine(line);
        } else {
            m_settings.parseGrblLine(line);
        }
        m_editBuffers.clear();
        m_collecting = true;
    } else if (m_collecting && (line == "ok" || line.substr(0, 5) == "error")) {
        if (m_collecting && !m_collectingSC &&
            m_firmwareType == FirmwareType::FluidNC) {
            // After $S completes, request $SC for changed-from-default
            m_collecting = false;
            m_collectingSC = true;
            if (m_cnc) m_cnc->sendCommand("$SC");
        } else {
            m_collecting = false;
            m_collectingSC = false;
        }
    }
}

void CncSettingsPanel::onStatusUpdate(const MachineStatus& status) {
    m_machineState = status.state;
}

} // namespace dw

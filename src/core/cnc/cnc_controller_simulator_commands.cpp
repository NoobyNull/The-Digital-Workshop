#include "cnc_controller.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace dw {

void CncController::simProcessCommand(const std::string& cmd) {
    if (cmd.empty()) return;

    // --- System commands ($) ---
    if (cmd[0] == '$') {
        if (cmd.find("$J=") == 0) {
            // Jog: $J=G91 G21 X10 F500
            std::string upper = cmd;
            for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            bool incremental = (upper.find("G91") != std::string::npos);

            auto parseVal = [&](char letter) -> std::pair<bool, float> {
                auto pos = upper.find(letter);
                if (pos != std::string::npos && pos + 1 < upper.size())
                    return {true, std::strtof(upper.c_str() + pos + 1, nullptr)};
                return {false, 0.0f};
            };

            auto [hx, xv] = parseVal('X');
            auto [hy, yv] = parseVal('Y');
            auto [hz, zv] = parseVal('Z');
            auto [hf, fv] = parseVal('F');

            if (hf && fv > 0.0f) m_sim.feedRate = fv;

            if (incremental) {
                if (hx) m_sim.targetPos.x = m_sim.machinePos.x + xv;
                if (hy) m_sim.targetPos.y = m_sim.machinePos.y + yv;
                if (hz) m_sim.targetPos.z = m_sim.machinePos.z + zv;
            } else {
                if (hx) m_sim.targetPos.x = xv;
                if (hy) m_sim.targetPos.y = yv;
                if (hz) m_sim.targetPos.z = zv;
            }
            m_sim.state = MachineState::Jog;
            simEmitOk();
        } else if (cmd == "$X" || cmd == "$x") {
            m_sim.state = MachineState::Idle;
            simEmitLine("[MSG:'$X' unlock]");
            simEmitOk();
        } else if (cmd == "$H" || cmd == "$h") {
            // Simulate homing: move to origin
            m_sim.machinePos = Vec3{0.0f};
            m_sim.targetPos = Vec3{0.0f};
            m_sim.state = MachineState::Idle;
            simEmitOk();
        } else if (cmd == "$$") {
            simHandleSettings(cmd);
        } else if (cmd == "$#") {
            simHandleHash();
        } else if (cmd == "$G" || cmd == "$g") {
            simHandleParserState();
        } else if (cmd == "$I" || cmd == "$i") {
            simHandleBuildInfo();
        } else if (cmd.size() > 1 && cmd[1] != '$' && cmd.find('=') != std::string::npos) {
            simHandleSettings(cmd); // $N=V setting write
        } else {
            simEmitOk();
        }
        return;
    }

    // --- G-code parsing ---
    std::string upper = cmd;
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    // Strip comments
    auto cpos = upper.find('(');
    if (cpos != std::string::npos) upper = upper.substr(0, cpos);
    cpos = upper.find(';');
    if (cpos != std::string::npos) upper = upper.substr(0, cpos);

    if (upper.empty()) { simEmitOk(); return; }

    auto parseAxis = [&](char letter) -> std::pair<bool, float> {
        auto pos = upper.find(letter);
        if (pos != std::string::npos && pos + 1 < upper.size())
            return {true, std::strtof(upper.c_str() + pos + 1, nullptr)};
        return {false, 0.0f};
    };

    // Check for specific G-code numbers
    auto hasGCode = [&](const char* g, int len) -> bool {
        auto pos = upper.find(g);
        if (pos == std::string::npos) return false;
        size_t after = pos + static_cast<size_t>(len);
        if (after < upper.size() && std::isdigit(static_cast<unsigned char>(upper[after])))
            return false; // e.g. G10 shouldn't match G1
        return true;
    };

    // --- Modal state changes ---
    if (hasGCode("G90", 3)) m_sim.absoluteMode = true;
    if (hasGCode("G91", 3)) m_sim.absoluteMode = false;
    if (hasGCode("G20", 3)) m_sim.metricMode = false;
    if (hasGCode("G21", 3)) m_sim.metricMode = true;

    // WCS selection
    if (hasGCode("G54", 3)) m_sim.activeWcs = 0;
    if (hasGCode("G55", 3)) m_sim.activeWcs = 1;
    if (hasGCode("G56", 3)) m_sim.activeWcs = 2;
    if (hasGCode("G57", 3)) m_sim.activeWcs = 3;
    if (hasGCode("G58", 3)) m_sim.activeWcs = 4;
    if (hasGCode("G59", 3)) m_sim.activeWcs = 5;

    // G10 L2/L20 — set WCS offset
    if (upper.find("G10") != std::string::npos) {
        auto [hl, lv] = parseAxis('L');
        auto [hp, pv] = parseAxis('P');
        if (hl) {
            int wcsIdx = static_cast<int>(pv);
            if (wcsIdx == 0) wcsIdx = m_sim.activeWcs + 1; // P0 = active WCS
            if (wcsIdx >= 1 && wcsIdx <= 6) {
                auto& wcs = m_sim.wcsOffsets[wcsIdx - 1];
                int lmode = static_cast<int>(lv);
                auto [hx, xv] = parseAxis('X');
                auto [hy, yv] = parseAxis('Y');
                auto [hz, zv] = parseAxis('Z');
                if (lmode == 2) {
                    // L2: set offset directly
                    if (hx) wcs.x = xv;
                    if (hy) wcs.y = yv;
                    if (hz) wcs.z = zv;
                } else if (lmode == 20) {
                    // L20: set offset so current position becomes the given value
                    if (hx) wcs.x = m_sim.machinePos.x - xv;
                    if (hy) wcs.y = m_sim.machinePos.y - yv;
                    if (hz) wcs.z = m_sim.machinePos.z - zv;
                }
            }
        }
        simEmitOk();
        return;
    }

    // G28/G30 — predefined positions
    if (hasGCode("G28", 3) && upper.find("G28.") == std::string::npos) {
        m_sim.targetPos = m_sim.g28Home;
        m_sim.isRapid = true;
    }
    if (hasGCode("G30", 3)) {
        m_sim.targetPos = m_sim.g30Home;
        m_sim.isRapid = true;
    }

    // G92 — coordinate offset
    if (hasGCode("G92", 3) && upper.find("G92.") == std::string::npos) {
        auto [hx, xv] = parseAxis('X');
        auto [hy, yv] = parseAxis('Y');
        auto [hz, zv] = parseAxis('Z');
        if (hx) m_sim.g92Offset.x = m_sim.machinePos.x - xv;
        if (hy) m_sim.g92Offset.y = m_sim.machinePos.y - yv;
        if (hz) m_sim.g92Offset.z = m_sim.machinePos.z - zv;
        simEmitOk();
        return;
    }
    // G92.1 — clear G92 offset
    if (upper.find("G92.1") != std::string::npos) {
        m_sim.g92Offset = Vec3{0.0f};
        simEmitOk();
        return;
    }

    // G38.2/G38.3 — probe (simulate contact partway to target)
    if (upper.find("G38.2") != std::string::npos || upper.find("G38.3") != std::string::npos) {
        auto [hx, xv] = parseAxis('X');
        auto [hy, yv] = parseAxis('Y');
        auto [hz, zv] = parseAxis('Z');

        Vec3 target = m_sim.machinePos;
        if (m_sim.absoluteMode) {
            Vec3 wcs = m_sim.wcsOffsets[m_sim.activeWcs];
            if (hx) target.x = xv + wcs.x + m_sim.g92Offset.x;
            if (hy) target.y = yv + wcs.y + m_sim.g92Offset.y;
            if (hz) target.z = zv + wcs.z + m_sim.g92Offset.z;
        } else {
            if (hx) target.x = m_sim.machinePos.x + xv;
            if (hy) target.y = m_sim.machinePos.y + yv;
            if (hz) target.z = m_sim.machinePos.z + zv;
        }

        if (hx) m_sim.machinePos.x += (target.x - m_sim.machinePos.x) * 0.5f;
        if (hy) m_sim.machinePos.y += (target.y - m_sim.machinePos.y) * 0.5f;
        if (hz) m_sim.machinePos.z += (target.z - m_sim.machinePos.z) * 0.5f;
        m_sim.targetPos = m_sim.machinePos;
        char buf[80];
        snprintf(buf, sizeof(buf), "[PRB:%.3f,%.3f,%.3f:1]",
            static_cast<double>(m_sim.machinePos.x),
            static_cast<double>(m_sim.machinePos.y),
            static_cast<double>(m_sim.machinePos.z));
        simEmitLine(buf);
        simEmitOk();
        return;
    }

    // --- Motion modes ---
    bool hasG0 = hasGCode("G0", 2) || hasGCode("G00", 3);
    bool hasG1 = hasGCode("G1", 2) || hasGCode("G01", 3);
    bool hasG2 = hasGCode("G2", 2) || hasGCode("G02", 3);
    bool hasG3 = hasGCode("G3", 2) || hasGCode("G03", 3);

    if (hasG0) { m_sim.motionMode = 0; m_sim.isRapid = true; }
    if (hasG1) { m_sim.motionMode = 1; m_sim.isRapid = false; }
    if (hasG2) m_sim.motionMode = 2;
    if (hasG3) m_sim.motionMode = 3;

    // Feed rate
    auto [hf, fv] = parseAxis('F');
    if (hf && fv > 0.0f) m_sim.feedRate = fv;

    // Axis words — compute target
    auto [hx, xv] = parseAxis('X');
    auto [hy, yv] = parseAxis('Y');
    auto [hz, zv] = parseAxis('Z');

    if (hx || hy || hz) {
        Vec3 wcs = m_sim.wcsOffsets[m_sim.activeWcs];
        if (m_sim.absoluteMode) {
            // Absolute: target = value + WCS offset
            if (hx) m_sim.targetPos.x = xv + wcs.x + m_sim.g92Offset.x;
            if (hy) m_sim.targetPos.y = yv + wcs.y + m_sim.g92Offset.y;
            if (hz) m_sim.targetPos.z = zv + wcs.z + m_sim.g92Offset.z;
        } else {
            // Incremental: target = current + delta
            if (hx) m_sim.targetPos.x = m_sim.machinePos.x + xv;
            if (hy) m_sim.targetPos.y = m_sim.machinePos.y + yv;
            if (hz) m_sim.targetPos.z = m_sim.machinePos.z + zv;
        }
    }

    // --- Spindle ---
    if (hasGCode("M3", 2) || hasGCode("M03", 3)) {
        m_sim.spindleDir = 3;
        auto [hs, sv] = parseAxis('S');
        if (hs) m_sim.spindleSpeed = sv;
        else if (m_sim.spindleSpeed == 0.0f) m_sim.spindleSpeed = 12000.0f;
    }
    if (hasGCode("M4", 2) || hasGCode("M04", 3)) {
        m_sim.spindleDir = 4;
        auto [hs, sv] = parseAxis('S');
        if (hs) m_sim.spindleSpeed = sv;
        else if (m_sim.spindleSpeed == 0.0f) m_sim.spindleSpeed = 12000.0f;
    }
    if (hasGCode("M5", 2) || hasGCode("M05", 3)) {
        m_sim.spindleDir = 0;
        m_sim.spindleSpeed = 0.0f;
    }

    // S word standalone
    auto [hasS, sVal] = parseAxis('S');
    if (hasS && m_sim.spindleDir != 0) m_sim.spindleSpeed = sVal;

    // --- Coolant ---
    if (hasGCode("M7", 2) || hasGCode("M07", 3)) m_sim.coolantMist = true;
    if (hasGCode("M8", 2) || hasGCode("M08", 3)) m_sim.coolantFlood = true;
    if (hasGCode("M9", 2) || hasGCode("M09", 3)) {
        m_sim.coolantMist = false;
        m_sim.coolantFlood = false;
    }

    // --- Tool ---
    auto [hasT, tv] = parseAxis('T');
    if (hasT) m_sim.toolNumber = static_cast<int>(tv);

    // --- Program pause ---
    if (hasGCode("M0", 2) || hasGCode("M00", 3) || hasGCode("M1", 2) || hasGCode("M01", 3)) {
        m_sim.state = MachineState::Hold;
    }

    simEmitOk();
}

std::string CncController::buildSimStatus() {
    const char* stateStr = "Idle";
    switch (m_sim.state) {
        case MachineState::Run:   stateStr = "Run"; break;
        case MachineState::Hold:  stateStr = "Hold:0"; break;
        case MachineState::Jog:   stateStr = "Jog"; break;
        case MachineState::Alarm: stateStr = "Alarm"; break;
        case MachineState::Home:  stateStr = "Home"; break;
        default: break;
    }

    // Compute work position = machine - WCS offset - G92 offset
    Vec3 wcs = m_sim.wcsOffsets[m_sim.activeWcs];
    Vec3 workPos = m_sim.machinePos - wcs - m_sim.g92Offset;

    int ovr = m_sim.isRapid ? m_sim.rapidOverride : m_sim.feedOverride;
    double feedDisplay = static_cast<double>(m_sim.feedRate) * (ovr / 100.0);

    char buf[300];
    snprintf(buf, sizeof(buf),
        "<%s|MPos:%.3f,%.3f,%.3f|WPos:%.3f,%.3f,%.3f|FS:%.0f,%.0f|Ov:%d,%d,%d>",
        stateStr,
        static_cast<double>(m_sim.machinePos.x), static_cast<double>(m_sim.machinePos.y),
        static_cast<double>(m_sim.machinePos.z),
        static_cast<double>(workPos.x), static_cast<double>(workPos.y),
        static_cast<double>(workPos.z),
        feedDisplay, static_cast<double>(m_sim.spindleSpeed),
        m_sim.feedOverride, m_sim.rapidOverride, m_sim.spindleOverride);
    return buf;
}

void CncController::simAdvancePosition(float dt) {
    if (m_sim.state == MachineState::Hold) return;

    Vec3 diff = m_sim.targetPos - m_sim.machinePos;
    float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
    if (dist < 0.001f) {
        m_sim.machinePos = m_sim.targetPos;
        if (m_sim.state == MachineState::Jog)
            m_sim.state = MachineState::Idle;
        return;
    }

    float rate = m_sim.isRapid
        ? m_sim.settings[110] * (static_cast<float>(m_sim.rapidOverride) / 100.0f)   // max X rate as rapid
        : m_sim.feedRate * (static_cast<float>(m_sim.feedOverride) / 100.0f);
    float speed = rate / 60.0f; // mm/s
    float move = speed * dt;

    if (move >= dist) {
        m_sim.machinePos = m_sim.targetPos;
        if (m_sim.state == MachineState::Jog)
            m_sim.state = MachineState::Idle;
    } else {
        float ratio = move / dist;
        m_sim.machinePos.x += diff.x * ratio;
        m_sim.machinePos.y += diff.y * ratio;
        m_sim.machinePos.z += diff.z * ratio;
    }
}

} // namespace dw

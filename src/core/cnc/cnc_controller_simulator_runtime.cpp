#include "cnc_controller.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "../threading/main_thread_queue.h"
#include "../utils/log.h"

namespace dw {

bool CncController::connectSimulator() {
    disconnect();

    m_simulating = true;
    m_sim = SimState{};
    setSendUnits(cnc::SendUnits::Millimeters);

    // Initialize default GRBL settings (common 3-axis CNC)
    m_sim.settings[0]  = 10.0f;   // $0  Step pulse time (usec)
    m_sim.settings[1]  = 25.0f;   // $1  Step idle delay (msec)
    m_sim.settings[2]  = 0.0f;    // $2  Step port invert mask
    m_sim.settings[3]  = 0.0f;    // $3  Direction port invert mask
    m_sim.settings[4]  = 0.0f;    // $4  Step enable invert
    m_sim.settings[5]  = 0.0f;    // $5  Limit pins invert
    m_sim.settings[6]  = 0.0f;    // $6  Probe pin invert
    m_sim.settings[10] = 1.0f;    // $10 Status report mask
    m_sim.settings[11] = 0.010f;  // $11 Junction deviation (mm)
    m_sim.settings[12] = 0.002f;  // $12 Arc tolerance (mm)
    m_sim.settings[13] = 0.0f;    // $13 Report inches (0=mm)
    m_sim.settings[20] = 0.0f;    // $20 Soft limits enable
    m_sim.settings[21] = 0.0f;    // $21 Hard limits enable
    m_sim.settings[22] = 1.0f;    // $22 Homing cycle enable
    m_sim.settings[23] = 0.0f;    // $23 Homing dir invert mask
    m_sim.settings[24] = 25.0f;   // $24 Homing feed (mm/min)
    m_sim.settings[25] = 500.0f;  // $25 Homing seek (mm/min)
    m_sim.settings[26] = 250.0f;  // $26 Homing debounce (msec)
    m_sim.settings[27] = 1.0f;    // $27 Homing pull-off (mm)
    m_sim.settings[30] = 24000.0f; // $30 Max spindle speed (RPM)
    m_sim.settings[31] = 0.0f;    // $31 Min spindle speed (RPM)
    m_sim.settings[32] = 0.0f;    // $32 Laser mode
    m_sim.settings[100] = 800.0f; // $100 X steps/mm
    m_sim.settings[101] = 800.0f; // $101 Y steps/mm
    m_sim.settings[102] = 800.0f; // $102 Z steps/mm
    m_sim.settings[110] = 5000.0f; // $110 X max rate (mm/min)
    m_sim.settings[111] = 5000.0f; // $111 Y max rate (mm/min)
    m_sim.settings[112] = 3000.0f; // $112 Z max rate (mm/min)
    m_sim.settings[120] = 500.0f; // $120 X acceleration (mm/sec^2)
    m_sim.settings[121] = 500.0f; // $121 Y acceleration (mm/sec^2)
    m_sim.settings[122] = 200.0f; // $122 Z acceleration (mm/sec^2)
    m_sim.settings[130] = 500.0f; // $130 X max travel (mm)
    m_sim.settings[131] = 500.0f; // $131 Y max travel (mm)
    m_sim.settings[132] = 100.0f; // $132 Z max travel (mm)
    m_sim.settingsInitialized = true;

    m_running = true;
    m_connected = false;
    m_pendingRtCommands.store(0, std::memory_order_relaxed);
    m_statusPollMs = 200;
    m_ioThread = std::thread(&CncController::simIoThreadFunc, this);

    return true;
}
void CncController::simEmitLine(const std::string& line) {
    if (m_mtq && m_callbacks.onRawLine)
        m_mtq->enqueue([cb = m_callbacks.onRawLine, line]() { cb(line, false); });
}

void CncController::simEmitOk() { simEmitLine("ok"); }

void CncController::simHandleSettings(const std::string& cmd) {
    // $N=V — write setting
    if (cmd.size() > 1 && cmd[1] != '$' && cmd.find('=') != std::string::npos) {
        int id = std::atoi(cmd.c_str() + 1);
        auto eqPos = cmd.find('=');
        float val = std::strtof(cmd.c_str() + eqPos + 1, nullptr);
        if (id >= 0 && id < 256) m_sim.settings[id] = val;
        simEmitOk();
        return;
    }
    // $$ — dump all settings
    static const int settingIds[] = {
        0,1,2,3,4,5,6,10,11,12,13,20,21,22,23,24,25,26,27,30,31,32,
        100,101,102,110,111,112,120,121,122,130,131,132
    };
    for (int id : settingIds) {
        char buf[64];
        if (m_sim.settings[id] == std::floor(m_sim.settings[id]))
            snprintf(buf, sizeof(buf), "$%d=%.0f", id, static_cast<double>(m_sim.settings[id]));
        else
            snprintf(buf, sizeof(buf), "$%d=%.3f", id, static_cast<double>(m_sim.settings[id]));
        simEmitLine(buf);
    }
    simEmitOk();
}

void CncController::simHandleHash() {
    // $# — emit WCS offsets
    auto emitVec = [this](const char* name, Vec3 v) {
        char buf[80];
        snprintf(buf, sizeof(buf), "[%s:%.3f,%.3f,%.3f]",
            name, static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z));
        simEmitLine(buf);
    };
    static const char* wcsNames[] = {"G54","G55","G56","G57","G58","G59"};
    for (int i = 0; i < 6; i++)
        emitVec(wcsNames[i], m_sim.wcsOffsets[i]);
    emitVec("G28", m_sim.g28Home);
    emitVec("G30", m_sim.g30Home);
    emitVec("G92", m_sim.g92Offset);
    char buf[64];
    snprintf(buf, sizeof(buf), "[TLO:%.3f]", static_cast<double>(m_sim.toolLengthOffset));
    simEmitLine(buf);
    simEmitOk();
}

void CncController::simHandleParserState() {
    // $G — emit parser state
    const char* motionStr = "G0";
    switch (m_sim.motionMode) {
        case 1: motionStr = "G1"; break;
        case 2: motionStr = "G2"; break;
        case 3: motionStr = "G3"; break;
    }
    static const char* wcsStrs[] = {"G54","G55","G56","G57","G58","G59"};
    const char* wcsStr = wcsStrs[m_sim.activeWcs % 6];
    char buf[128];
    snprintf(buf, sizeof(buf), "[GC:%s %s %s %s G17 G40 G49 G94 M%d M9 T%d F%.0f S%.0f]",
        motionStr,
        m_sim.absoluteMode ? "G90" : "G91",
        m_sim.metricMode ? "G21" : "G20",
        wcsStr,
        m_sim.spindleDir,
        m_sim.toolNumber,
        static_cast<double>(m_sim.feedRate),
        static_cast<double>(m_sim.spindleSpeed));
    simEmitLine(buf);
    simEmitOk();
}

void CncController::simHandleBuildInfo() {
    simEmitLine("[VER:1.1h.20190825 Simulator]");
    simEmitLine("[OPT:V,15,128]");
    simEmitOk();
}

void CncController::simIoThreadFunc() {
    log::info("CNC", "Simulator IO thread started");

    std::string version = "Grbl 1.1h [Simulator]";
    m_connected = true;
    log::infof("CNC", "Simulator connected: %s", version.c_str());
    if (m_mtq && m_callbacks.onConnectionChanged)
        m_mtq->enqueue([cb = m_callbacks.onConnectionChanged, ver = version]() { cb(true, ver); });
    simEmitLine(version);

    auto lastStatusTime = std::chrono::steady_clock::now();
    auto lastTick = lastStatusTime;

    while (m_running) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastTick).count();
        lastTick = now;

        // --- Real-time commands ---
        uint32_t pending = m_pendingRtCommands.exchange(0, std::memory_order_acquire);
        if (pending & RT_SOFT_RESET) {
            m_sim.state = MachineState::Idle;
            m_sim.targetPos = m_sim.machinePos;
            m_streaming = false;
            m_held = false;
            {
                std::lock_guard<std::mutex> lock(m_streamMutex);
                m_sentLengths.clear();
                m_bufferUsed = 0;
            }
            simEmitLine(version);
        }
        if (pending & RT_FEED_HOLD) {
            if (m_sim.state == MachineState::Run || m_sim.state == MachineState::Jog)
                m_sim.state = MachineState::Hold;
        }
        if (pending & RT_CYCLE_START) {
            if (m_sim.state == MachineState::Hold)
                m_sim.state = m_streaming ? MachineState::Run : MachineState::Idle;
        }
        if (pending & RT_JOG_CANCEL) {
            if (m_sim.state == MachineState::Jog) {
                m_sim.targetPos = m_sim.machinePos;
                m_sim.state = MachineState::Idle;
            }
        }

        // --- Overrides ---
        {
            std::lock_guard<std::mutex> lock(m_overrideMutex);
            for (const auto& cmd : m_pendingOverrides) {
                for (u8 b : cmd.bytes) {
                    if (b == cnc::CMD_FEED_100_PERCENT)       m_sim.feedOverride = 100;
                    else if (b == cnc::CMD_FEED_PLUS_10)      m_sim.feedOverride = std::min(200, m_sim.feedOverride + 10);
                    else if (b == cnc::CMD_FEED_MINUS_10)     m_sim.feedOverride = std::max(10,  m_sim.feedOverride - 10);
                    else if (b == cnc::CMD_FEED_PLUS_1)       m_sim.feedOverride = std::min(200, m_sim.feedOverride + 1);
                    else if (b == cnc::CMD_FEED_MINUS_1)      m_sim.feedOverride = std::max(10,  m_sim.feedOverride - 1);
                    else if (b == cnc::CMD_RAPID_100_PERCENT)  m_sim.rapidOverride = 100;
                    else if (b == cnc::CMD_RAPID_50_PERCENT)   m_sim.rapidOverride = 50;
                    else if (b == cnc::CMD_RAPID_25_PERCENT)   m_sim.rapidOverride = 25;
                    else if (b == cnc::CMD_SPINDLE_100_PERCENT) m_sim.spindleOverride = 100;
                    else if (b == cnc::CMD_SPINDLE_PLUS_10)   m_sim.spindleOverride = std::min(200, m_sim.spindleOverride + 10);
                    else if (b == cnc::CMD_SPINDLE_MINUS_10)  m_sim.spindleOverride = std::max(10,  m_sim.spindleOverride - 10);
                    else if (b == cnc::CMD_SPINDLE_PLUS_1)    m_sim.spindleOverride = std::min(200, m_sim.spindleOverride + 1);
                    else if (b == cnc::CMD_SPINDLE_MINUS_1)   m_sim.spindleOverride = std::max(10,  m_sim.spindleOverride - 1);
                }
            }
            m_pendingOverrides.clear();
        }

        // --- String commands ---
        {
            std::vector<std::string> cmds;
            {
                std::lock_guard<std::mutex> lock(m_cmdStringMutex);
                cmds.swap(m_pendingStringCmds);
            }
            for (auto& cmd : cmds) {
                while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r'))
                    cmd.pop_back();
                if (m_mtq && m_callbacks.onRawLine)
                    m_mtq->enqueue([cb = m_callbacks.onRawLine, c = cmd]() { cb(c, true); });
                simProcessCommand(cmd);
            }
        }

        // --- Streaming ---
        if (m_streaming && !m_held && m_sim.state != MachineState::Hold) {
            std::lock_guard<std::mutex> lock(m_streamMutex);
            if (!m_toolChangePending.load() && m_ackIndex < static_cast<int>(m_program.size())) {
                const std::string& line = m_program[static_cast<size_t>(m_ackIndex)];

                // Check for M6 tool change
                std::string upper = line;
                for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                auto cpos = upper.find('(');
                if (cpos != std::string::npos) upper = upper.substr(0, cpos);
                cpos = upper.find(';');
                if (cpos != std::string::npos) upper = upper.substr(0, cpos);

                bool hasM6 = false;
                auto m6p = upper.find("M6");
                if (m6p != std::string::npos) {
                    size_t after = m6p + 2;
                    if (after >= upper.size() || !std::isdigit(static_cast<unsigned char>(upper[after])))
                        hasM6 = true;
                }
                if (!hasM6) {
                    auto m06p = upper.find("M06");
                    if (m06p != std::string::npos) {
                        size_t after = m06p + 3;
                        if (after >= upper.size() || !std::isdigit(static_cast<unsigned char>(upper[after])))
                            hasM6 = true;
                    }
                }

                if (hasM6) {
                    int toolNum = 0;
                    auto tPos = upper.find('T');
                    if (tPos != std::string::npos)
                        toolNum = std::atoi(upper.c_str() + tPos + 1);
                    m_toolChangePending = true;
                    if (m_mtq && m_callbacks.onToolChange)
                        m_mtq->enqueue([cb = m_callbacks.onToolChange, toolNum]() { cb(toolNum); });
                } else {
                    if (m_mtq && m_callbacks.onRawLine)
                        m_mtq->enqueue([cb = m_callbacks.onRawLine, l = line]() { cb(l, true); });

                    simProcessCommand(line);
                    m_sim.state = MachineState::Run;

                    LineAck ack;
                    ack.lineIndex = m_ackIndex;
                    ack.ok = true;
                    m_ackIndex++;

                    simEmitLine("ok");
                    if (m_mtq && m_callbacks.onLineAcked)
                        m_mtq->enqueue([cb = m_callbacks.onLineAcked, ack]() { cb(ack); });
                    if (m_ackIndex >= static_cast<int>(m_program.size())) {
                        m_streaming = false;
                        m_sim.state = MachineState::Idle;
                    }
                    if (m_mtq && m_callbacks.onProgressUpdate) {
                        StreamProgress prog;
                        prog.totalLines = static_cast<int>(m_program.size());
                        prog.ackedLines = m_ackIndex;
                        prog.errorCount = m_errorCount;
                        prog.streaming = m_streaming.load();
                        prog.elapsedSeconds = std::chrono::duration<f32>(
                            std::chrono::steady_clock::now() - m_streamStartTime).count();
                        m_mtq->enqueue([cb = m_callbacks.onProgressUpdate, prog]() { cb(prog); });
                    }
                }
            }
        }

        // --- Position advance ---
        simAdvancePosition(dt);

        // --- Status polling ---
        auto statusElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStatusTime).count();
        if (statusElapsed >= m_statusPollMs) {
            lastStatusTime = now;
            std::string statusStr = buildSimStatus();
            MachineStatus status = parseStatusReport(statusStr);
            m_lastStatus = status;
            if (m_mtq && m_callbacks.onStatusUpdate)
                m_mtq->enqueue([cb = m_callbacks.onStatusUpdate, status]() { cb(status); });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    m_connected = false;
    log::info("CNC", "Simulator IO thread stopped");
}

} // namespace dw

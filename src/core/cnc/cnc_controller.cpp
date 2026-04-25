#include "cnc_controller.h"
#include "serial_port.h"
#include "tcp_socket.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "../config/config.h"
#include "../threading/main_thread_queue.h"
#include "../utils/log.h"

namespace dw {

CncController::CncController(MainThreadQueue* mtq) : m_mtq(mtq) {}

CncController::~CncController() {
    disconnect();
}

bool CncController::connect(const std::string& device, int baudRate) {
    disconnect();

    auto port = std::make_unique<SerialPort>();
    if (!port->open(device, baudRate))
        return false;

    m_port = std::move(port);

    // Soft-reset to get a clean state
    m_port->writeByte(cnc::CMD_SOFT_RESET);
    m_port->drain();

    initializeConnection();

    return true;
}

bool CncController::connectTcp(const std::string& host, int port) {
    disconnect();

    auto sock = std::make_unique<TcpSocket>();
    if (!sock->connect(host, port))
        return false;

    m_port = std::move(sock);

    // Soft-reset to get a clean state (same as serial connect)
    m_port->writeByte(cnc::CMD_SOFT_RESET);
    m_port->drain();

    initializeConnection();

    return true;
}

void CncController::initializeConnection() {
    m_running = true;
    m_connected = false;
    setSendUnits(cnc::SendUnits::Millimeters);
    m_consecutiveTimeouts = 0;
    m_statusPending = false;
    m_pendingRtCommands.store(0, std::memory_order_relaxed);
    m_statusPollMs = Config::instance().getStatusPollIntervalMs();
    m_ioThread = std::thread(&CncController::ioThreadFunc, this);
}

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

void CncController::disconnect() {
    m_running = false;
    m_streaming = false;
    m_errorState = false;
    postProgressUpdate();

    if (m_ioThread.joinable())
        m_ioThread.join();

    // Clear any pending commands
    m_pendingRtCommands.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(m_overrideMutex);
        m_pendingOverrides.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_cmdStringMutex);
        m_pendingStringCmds.clear();
    }
    m_consecutiveTimeouts = 0;
    m_statusPending = false;
    setSendUnits(cnc::SendUnits::Millimeters);

    bool wasConnected = m_connected.exchange(false);
    bool wasSim = m_simulating.exchange(false);

    if (wasSim) {
        // Simulator mode — no serial port to close
        if (wasConnected && m_mtq && m_callbacks.onConnectionChanged)
            m_mtq->enqueue([cb = m_callbacks.onConnectionChanged]() { cb(false, ""); });
    } else if (m_port && m_port->isOpen()) {
        m_port->close();
        m_port.reset();

        if (wasConnected && m_mtq && m_callbacks.onConnectionChanged)
            m_mtq->enqueue([cb = m_callbacks.onConnectionChanged]() { cb(false, ""); });
    }
}

void CncController::startStream(const std::vector<std::string>& lines) {
    if (m_errorState) {
        log::error("CNC", "Cannot start stream while in error state -- call acknowledgeError() first");
        if (m_mtq && m_callbacks.onError) {
            m_mtq->enqueue([cb = m_callbacks.onError]() {
                cb("Cannot start new job: previous streaming error must be acknowledged first");
            });
        }
        return;
    }
    std::lock_guard<std::mutex> lock(m_streamMutex);
    m_program = lines;
    m_sendIndex = 0;
    m_ackIndex = 0;
    m_sentLengths.clear();
    m_bufferUsed = 0;
    m_errorCount = 0;
    m_held = false;
    m_toolChangePending = false;
    m_streamStartTime = std::chrono::steady_clock::now();
    m_streaming = true;
    postProgressUpdate();
}

void CncController::acknowledgeError() {
    m_errorState = false;
    log::info("CNC", "Streaming error acknowledged by operator");
}

void CncController::acknowledgeToolChange() {
    if (m_toolChangePending.load()) {
        m_toolChangePending = false;
        log::info("CNC", "Tool change acknowledged by operator -- resuming stream");
        // Skip the M6 line (GRBL doesn't implement M6 natively)
        {
            std::lock_guard<std::mutex> lock(m_streamMutex);
            if (m_sendIndex < static_cast<int>(m_program.size()))
                m_sendIndex++;
        }
    }
}

void CncController::stopStream() {
    m_streaming = false;
    postProgressUpdate();
    // Send feed hold + soft reset to stop the machine
    feedHold();
}

// --- Real-time commands (UI thread safe — all routed through IO thread) ---

void CncController::feedHold() {
    m_pendingRtCommands.fetch_or(RT_FEED_HOLD, std::memory_order_release);
    m_held = true;
}

void CncController::cycleStart() {
    m_pendingRtCommands.fetch_or(RT_CYCLE_START, std::memory_order_release);
    m_held = false;
}

void CncController::softReset() {
    m_pendingRtCommands.fetch_or(RT_SOFT_RESET, std::memory_order_release);
    m_streaming = false;
    m_held = false;
    m_errorState = false; // Explicit reset clears error state
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        m_sentLengths.clear();
        m_bufferUsed = 0;
    }
    postProgressUpdate();
    // drain() will be called by the IO thread after dispatching the reset
}

void CncController::setFeedOverride(int percent) {
    OverrideCmd cmd;
    cmd.bytes.push_back(cnc::CMD_FEED_100_PERCENT);
    int diff = percent - 100;
    while (diff >= 10)  { cmd.bytes.push_back(cnc::CMD_FEED_PLUS_10);  diff -= 10; }
    while (diff <= -10) { cmd.bytes.push_back(cnc::CMD_FEED_MINUS_10); diff += 10; }
    while (diff > 0)    { cmd.bytes.push_back(cnc::CMD_FEED_PLUS_1);   diff--; }
    while (diff < 0)    { cmd.bytes.push_back(cnc::CMD_FEED_MINUS_1);  diff++; }
    {
        std::lock_guard<std::mutex> lock(m_overrideMutex);
        m_pendingOverrides.push_back(std::move(cmd));
    }
}

void CncController::setRapidOverride(int percent) {
    OverrideCmd cmd;
    if (percent <= 25)
        cmd.bytes.push_back(cnc::CMD_RAPID_25_PERCENT);
    else if (percent <= 50)
        cmd.bytes.push_back(cnc::CMD_RAPID_50_PERCENT);
    else
        cmd.bytes.push_back(cnc::CMD_RAPID_100_PERCENT);
    {
        std::lock_guard<std::mutex> lock(m_overrideMutex);
        m_pendingOverrides.push_back(std::move(cmd));
    }
}

void CncController::setSpindleOverride(int percent) {
    OverrideCmd cmd;
    cmd.bytes.push_back(cnc::CMD_SPINDLE_100_PERCENT);
    int diff = percent - 100;
    while (diff >= 10)  { cmd.bytes.push_back(cnc::CMD_SPINDLE_PLUS_10);  diff -= 10; }
    while (diff <= -10) { cmd.bytes.push_back(cnc::CMD_SPINDLE_MINUS_10); diff += 10; }
    while (diff > 0)    { cmd.bytes.push_back(cnc::CMD_SPINDLE_PLUS_1);   diff--; }
    while (diff < 0)    { cmd.bytes.push_back(cnc::CMD_SPINDLE_MINUS_1);  diff++; }
    {
        std::lock_guard<std::mutex> lock(m_overrideMutex);
        m_pendingOverrides.push_back(std::move(cmd));
    }
}

void CncController::jogCancel() {
    m_pendingRtCommands.fetch_or(RT_JOG_CANCEL, std::memory_order_release);
}

void CncController::unlock() {
    std::lock_guard<std::mutex> lock(m_cmdStringMutex);
    m_pendingStringCmds.push_back("$X\n");
}

void CncController::sendCommand(const std::string& cmd) {
    std::lock_guard<std::mutex> lock(m_cmdStringMutex);
    m_pendingStringCmds.push_back(cmd + "\n");
}

cnc::SendUnits CncController::sendUnits() const {
    return m_sendUnits.load(std::memory_order_acquire) == 1
        ? cnc::SendUnits::Inches
        : cnc::SendUnits::Millimeters;
}

void CncController::setSendUnits(cnc::SendUnits units) {
    m_sendUnits.store(units == cnc::SendUnits::Inches ? 1 : 0,
                      std::memory_order_release);
}

StreamProgress CncController::streamProgress() const {
    StreamProgress p;
    // Read atomic/shared state — approximate is fine for UI display
    p.totalLines = static_cast<int>(m_program.size());
    p.ackedLines = m_ackIndex;
    p.errorCount = m_errorCount;
    p.streaming = m_streaming.load();

    auto now = std::chrono::steady_clock::now();
    p.elapsedSeconds = std::chrono::duration<f32>(now - m_streamStartTime).count();
    return p;
}

void CncController::postProgressUpdate() {
    if (!m_mtq || !m_callbacks.onProgressUpdate)
        return;

    auto prog = streamProgress();
    m_mtq->enqueue([cb = m_callbacks.onProgressUpdate, prog]() { cb(prog); });
}

// --- IO Thread ---

void CncController::ioThreadFunc() {
    log::info("CNC", "IO thread started");

    // Wait for controller banner or probe with status query.
    // Classic Grbl sends a banner on reset; FluidNC may not.
    std::string version;
    for (int i = 0; i < 50; ++i) { // 5 seconds max
        auto line = m_port->readLine(100);
        if (!line)
            continue;
        // Classic Grbl: "Grbl 1.1h ['$' for help]"
        // FluidNC:      "[MSG:INFO: FluidNC v3.7.x ...]"
        // grblHAL:      "GrblHAL 1.1f ..."
        if (line->find("Grbl") != std::string::npos ||
            line->find("grbl") != std::string::npos ||
            line->find("FluidNC") != std::string::npos) {
            version = *line;
            break;
        }
    }

    // If no banner, probe with '?' status query — FluidNC responds without a banner
    if (version.empty()) {
        m_port->write("?\n");
        for (int i = 0; i < 20; ++i) { // 2 seconds
            auto line = m_port->readLine(100);
            if (line && line->size() > 1 && (*line)[0] == '<') {
                // Got a valid Grbl status response like "<Idle|MPos:...>"
                version = "FluidNC (compatible)";
                break;
            }
        }
    }

    if (version.empty()) {
        log::error("CNC", "No compatible controller detected");
        m_running = false;
        if (m_mtq && m_callbacks.onConnectionChanged)
            m_mtq->enqueue([cb = m_callbacks.onConnectionChanged]() { cb(false, ""); });
        return;
    }

    // Detect firmware type from banner
    if (version.find("FluidNC") != std::string::npos) {
        m_firmwareType = FirmwareType::FluidNC;
    } else if (version.find("GrblHAL") != std::string::npos ||
               version.find("grblHAL") != std::string::npos) {
        m_firmwareType = FirmwareType::GrblHAL;
    } else {
        m_firmwareType = FirmwareType::GRBL;
    }

    m_connected = true;
    log::infof("CNC", "Connected: %s (firmware: %s)", version.c_str(),
               m_firmwareType == FirmwareType::FluidNC ? "FluidNC" :
               m_firmwareType == FirmwareType::GrblHAL ? "grblHAL" : "GRBL");
    if (m_mtq && m_callbacks.onConnectionChanged) {
        m_mtq->enqueue(
            [cb = m_callbacks.onConnectionChanged, ver = version]() { cb(true, ver); });
    }

    m_lastStatusQuery = std::chrono::steady_clock::now();

    while (m_running) {
        // Dispatch any pending commands from UI thread (feed hold, cycle start, etc.)
        dispatchPendingCommands();

        // Read responses
        auto line = m_port->readLine(20);

        // Check SerialPort connection state for hardware-level disconnect
        if (m_port->connectionState() == ConnectionState::Disconnected ||
            m_port->connectionState() == ConnectionState::Error) {
            log::error("CNC", "Serial port reports disconnected");
            handleDisconnect();
            break;
        }

        if (line) {
            m_consecutiveTimeouts = 0;
            if (m_mtq && m_callbacks.onRawLine) {
                m_mtq->enqueue(
                    [cb = m_callbacks.onRawLine, l = *line]() { cb(l, false); });
            }
            processResponse(*line);
        } else {
            // No data -- check for consecutive timeout disconnect detection
            if (m_statusPending) {
                m_consecutiveTimeouts++;
                if (m_consecutiveTimeouts >= MAX_CONSECUTIVE_TIMEOUTS) {
                    log::error("CNC", "No response to status queries -- connection lost");
                    handleDisconnect();
                    break;
                }
            }
        }

        // Status polling at 5Hz
        auto now = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastStatusQuery).count();
        if (elapsed >= m_statusPollMs) {
            requestStatus();
            m_lastStatusQuery = now;
        }

        // Character-counting: send more lines if buffer has room
        if (m_streaming && !m_held) {
            sendNextLines();
        }
    }

    m_connected = false;
    log::info("CNC", "IO thread stopped");
}

void CncController::processResponse(const std::string& line) {
    if (line.empty())
        return;

    // Status report: <Idle|MPos:0.000,0.000,0.000|...>
    if (line.front() == '<' && line.back() == '>') {
        m_lastStatus = parseStatusReport(line);
        m_statusPending = false;
        m_consecutiveTimeouts = 0;
        if (m_mtq && m_callbacks.onStatusUpdate) {
            m_mtq->enqueue(
                [cb = m_callbacks.onStatusUpdate, st = m_lastStatus]() { cb(st); });
        }
        return;
    }

    // Alarm
    if (line.find("ALARM:") == 0) {
        int code = 0;
        try {
            code = std::stoi(line.substr(6));
        } catch (...) {}
        std::string desc = alarmDescription(code);
        if (m_mtq && m_callbacks.onAlarm) {
            m_mtq->enqueue([cb = m_callbacks.onAlarm, code, desc]() { cb(code, desc); });
        }
        // Stop streaming on alarm
        m_streaming = false;
        postProgressUpdate();
        return;
    }

    // "ok" or "error:N" — ack for a sent line
    if (line == "ok" || line.find("error:") == 0) {
        std::lock_guard<std::mutex> lock(m_streamMutex);

        if (!m_sentLengths.empty()) {
            m_bufferUsed -= m_sentLengths.front();
            m_sentLengths.pop_front();
        }

        LineAck ack;
        ack.lineIndex = m_ackIndex;
        ack.ok = (line == "ok");

        if (!ack.ok) {
            try {
                ack.errorCode = std::stoi(line.substr(6));
            } catch (...) {}
            ack.errorMessage = errorDescription(ack.errorCode);
            m_errorCount++;

            if (m_streaming) {
                // CRITICAL SAFETY: Issue soft reset to flush GRBL's RX buffer.
                // Without this, buffered commands continue executing in potentially
                // incorrect machine state after the error.
                m_pendingRtCommands.fetch_or(RT_SOFT_RESET, std::memory_order_release);

                // Capture error details before clearing state
                StreamingError streamErr;
                streamErr.lineIndex = ack.lineIndex;
                streamErr.errorCode = ack.errorCode;
                streamErr.errorMessage = ack.errorMessage;
                if (ack.lineIndex >= 0 && ack.lineIndex < static_cast<int>(m_program.size()))
                    streamErr.failedLine = m_program[static_cast<size_t>(ack.lineIndex)];
                streamErr.linesInFlight = static_cast<int>(m_sentLengths.size());

                // Stop streaming and clear buffer accounting
                m_streaming = false;
                m_held = false;
                m_sentLengths.clear();
                m_bufferUsed = 0;
                auto prog = streamProgress();

                // Enter error state -- requires acknowledgment before new operations
                m_errorState = true;

                // Notify UI with detailed error report
                if (m_mtq && m_callbacks.onStreamingError) {
                    m_mtq->enqueue([cb = m_callbacks.onStreamingError, streamErr]() {
                        cb(streamErr);
                    });
                }
                if (m_mtq && m_callbacks.onProgressUpdate) {
                    m_mtq->enqueue([cb = m_callbacks.onProgressUpdate, prog]() { cb(prog); });
                }

                // Also fire the line ack callback so UI can track the specific line
                if (m_mtq && m_callbacks.onLineAcked) {
                    m_mtq->enqueue([cb = m_callbacks.onLineAcked, ack]() { cb(ack); });
                }
                return; // Don't process further -- stream is terminated
            }
        }

        m_ackIndex++;

        if (m_mtq && m_callbacks.onLineAcked) {
            m_mtq->enqueue([cb = m_callbacks.onLineAcked, ack]() { cb(ack); });
        }

        // Check if stream complete
        if (m_streaming && m_ackIndex >= static_cast<int>(m_program.size())) {
            m_streaming = false;
        }

        // Post progress update
        if (m_mtq && m_callbacks.onProgressUpdate) {
            auto prog = streamProgress();
            m_mtq->enqueue([cb = m_callbacks.onProgressUpdate, prog]() { cb(prog); });
        }
        return;
    }

    // GRBL messages like [MSG:...], [GC:...], etc.
    if (line.front() == '[') {
        if (m_mtq && m_callbacks.onError && line.find("[MSG:") == 0) {
            std::string msg = line.substr(5);
            if (!msg.empty() && msg.back() == ']')
                msg.pop_back();
            m_mtq->enqueue([cb = m_callbacks.onError, msg]() { cb(msg); });
        }
    }
}

void CncController::sendNextLines() {
    std::lock_guard<std::mutex> lock(m_streamMutex);

    // If tool change pending, don't send more lines until acknowledged
    if (m_toolChangePending.load()) return;

    while (m_sendIndex < static_cast<int>(m_program.size())) {
        const std::string& line = m_program[static_cast<size_t>(m_sendIndex)];

        // Check for M6 tool change before sending (GRBL doesn't implement M6)
        {
            std::string upper = line;
            for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

            // Strip comments
            auto commentPos = upper.find('(');
            if (commentPos != std::string::npos) upper = upper.substr(0, commentPos);
            commentPos = upper.find(';');
            if (commentPos != std::string::npos) upper = upper.substr(0, commentPos);

            bool hasM6 = false;
            auto m6pos = upper.find("M6");
            auto m06pos = upper.find("M06");

            if (m6pos != std::string::npos) {
                // Check it's M6 not M60+
                size_t afterM6 = m6pos + 2;
                if (afterM6 >= upper.size() || !std::isdigit(static_cast<unsigned char>(upper[afterM6]))) {
                    hasM6 = true;
                }
            }
            if (!hasM6 && m06pos != std::string::npos) {
                size_t afterM06 = m06pos + 3;
                if (afterM06 >= upper.size() || !std::isdigit(static_cast<unsigned char>(upper[afterM06]))) {
                    hasM6 = true;
                }
            }

            if (hasM6) {
                // Parse tool number from T word if present
                int toolNum = 0;
                auto tPos = upper.find('T');
                if (tPos != std::string::npos) {
                    toolNum = std::atoi(upper.c_str() + tPos + 1);
                }

                m_toolChangePending = true;
                if (m_mtq && m_callbacks.onToolChange) {
                    m_mtq->enqueue([cb = m_callbacks.onToolChange, toolNum]() {
                        cb(toolNum);
                    });
                }
                return; // Don't send the M6 line -- wait for operator ack
            }
        }

        // Character counting: each line uses (length + 1) bytes in GRBL buffer (for the \n)
        int lineLen = static_cast<int>(line.size()) + 1;

        if (m_bufferUsed + lineLen > cnc::RX_BUFFER_SIZE)
            break; // Buffer full, wait for acks

        std::string toSend = line + "\n";
        if (!m_port->write(toSend))
            break;

        if (m_mtq && m_callbacks.onRawLine) {
            m_mtq->enqueue(
                [cb = m_callbacks.onRawLine, l = line]() { cb(l, true); });
        }

        m_sentLengths.push_back(lineLen);
        m_bufferUsed += lineLen;
        m_sendIndex++;
    }
}

void CncController::requestStatus() {
    m_port->writeByte(cnc::CMD_STATUS_QUERY);
    m_statusPending = true;
}

void CncController::dispatchPendingCommands() {
    // 1. Dispatch single-byte real-time commands (atomic, no lock needed)
    uint32_t pending = m_pendingRtCommands.exchange(0, std::memory_order_acquire);

    // Soft reset has highest priority — after reset, don't send anything else
    if (pending & RT_SOFT_RESET) {
        m_port->writeByte(cnc::CMD_SOFT_RESET);
        m_port->drain();
        return;
    }
    if (pending & RT_FEED_HOLD)   m_port->writeByte(cnc::CMD_FEED_HOLD);
    if (pending & RT_CYCLE_START) m_port->writeByte(cnc::CMD_CYCLE_START);
    if (pending & RT_JOG_CANCEL)  m_port->writeByte(0x85);

    // 2. Dispatch override byte sequences (atomically per override)
    {
        std::lock_guard<std::mutex> lock(m_overrideMutex);
        for (const auto& cmd : m_pendingOverrides) {
            for (u8 b : cmd.bytes)
                m_port->writeByte(b);
        }
        m_pendingOverrides.clear();
    }

    // 3. Dispatch string commands (e.g., $X unlock)
    {
        std::lock_guard<std::mutex> lock(m_cmdStringMutex);
        for (const auto& s : m_pendingStringCmds)
            m_port->write(s);
        m_pendingStringCmds.clear();
    }
}

void CncController::handleDisconnect() {
    m_connected = false;
    bool wasStreaming = m_streaming.exchange(false);
    m_held = false;

    // Clear streaming state
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        m_sentLengths.clear();
        m_bufferUsed = 0;
    }

    // Notify UI of disconnect
    if (m_mtq && m_callbacks.onConnectionChanged)
        m_mtq->enqueue([cb = m_callbacks.onConnectionChanged]() {
            cb(false, "");
        });

    if (wasStreaming && m_mtq && m_callbacks.onError)
        m_mtq->enqueue([cb = m_callbacks.onError]() {
            cb("Connection lost during streaming -- job aborted. Manual reconnect required.");
        });
}

// --- Simulator ---

// Helper: emit a line as if received from GRBL
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

// --- Static parsers ---

MachineState CncController::parseState(const std::string& stateStr) {
    if (stateStr == "Idle") return MachineState::Idle;
    if (stateStr == "Run") return MachineState::Run;
    if (stateStr == "Hold" || stateStr.find("Hold:") == 0) return MachineState::Hold;
    if (stateStr == "Jog") return MachineState::Jog;
    if (stateStr == "Alarm") return MachineState::Alarm;
    if (stateStr == "Door" || stateStr.find("Door:") == 0) return MachineState::Door;
    if (stateStr == "Check") return MachineState::Check;
    if (stateStr == "Home") return MachineState::Home;
    if (stateStr == "Sleep") return MachineState::Sleep;
    return MachineState::Unknown;
}

MachineStatus CncController::parseStatusReport(const std::string& report) {
    MachineStatus status;

    // Format: <State|MPos:x,y,z|WPos:x,y,z|FS:feed,speed|Ov:f,r,s|Pn:XYZ>
    if (report.size() < 3)
        return status;

    // Strip < and >
    std::string inner = report.substr(1, report.size() - 2);

    // Split by |
    std::vector<std::string> fields;
    std::istringstream stream(inner);
    std::string field;
    while (std::getline(stream, field, '|'))
        fields.push_back(field);

    if (fields.empty())
        return status;

    // First field is state
    status.state = parseState(fields[0]);

    // Parse remaining fields
    for (size_t i = 1; i < fields.size(); ++i) {
        const auto& f = fields[i];
        auto colonPos = f.find(':');
        if (colonPos == std::string::npos)
            continue;

        std::string key = f.substr(0, colonPos);
        std::string val = f.substr(colonPos + 1);

        auto parseVec3 = [](const std::string& s) -> Vec3 {
            Vec3 v{0.0f};
            if (sscanf(s.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z) != 3) {
                // Try 2-axis
                sscanf(s.c_str(), "%f,%f", &v.x, &v.y);
            }
            return v;
        };

        if (key == "MPos") {
            status.machinePos = parseVec3(val);
        } else if (key == "WPos") {
            status.workPos = parseVec3(val);
        } else if (key == "WCO") {
            // Work coordinate offset — derive WPos from MPos - WCO
            Vec3 wco = parseVec3(val);
            status.workPos = status.machinePos - wco;
        } else if (key == "FS" || key == "F") {
            if (key == "FS") {
                sscanf(val.c_str(), "%f,%f", &status.feedRate, &status.spindleSpeed);
            } else {
                sscanf(val.c_str(), "%f", &status.feedRate);
            }
        } else if (key == "Ov") {
            sscanf(val.c_str(), "%d,%d,%d", &status.feedOverride, &status.rapidOverride,
                   &status.spindleOverride);
        } else if (key == "Pn") {
            status.inputPins = 0; // Clear all pins first
            for (char c : val) {
                switch (c) {
                case 'X': status.inputPins |= cnc::PIN_X_LIMIT; break;
                case 'Y': status.inputPins |= cnc::PIN_Y_LIMIT; break;
                case 'Z': status.inputPins |= cnc::PIN_Z_LIMIT; break;
                case 'P': status.inputPins |= cnc::PIN_PROBE; break;
                case 'D': status.inputPins |= cnc::PIN_DOOR; break;
                case 'H': status.inputPins |= cnc::PIN_HOLD; break;
                case 'R': status.inputPins |= cnc::PIN_RESET; break;
                case 'S': status.inputPins |= cnc::PIN_START; break;
                }
            }
        }
    }

    return status;
}

} // namespace dw

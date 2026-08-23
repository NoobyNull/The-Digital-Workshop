#include "cnc_controller.h"
#include "serial_port.h"
#include "tcp_socket.h"

#include <cctype>
#include <cstdlib>

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
} // namespace dw

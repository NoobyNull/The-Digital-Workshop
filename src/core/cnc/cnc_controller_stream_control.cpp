// Protected program-stream and real-time control surface for CncController.

#include "cnc_controller.h"

#include <chrono>
#include <utility>

#include "../threading/main_thread_queue.h"
#include "../utils/log.h"

namespace dw {

bool CncController::startStream(const std::vector<std::string>& lines) {
    if (!m_connected.load()) {
        log::error("CNC", "Cannot start stream while controller is disconnected");
        return false;
    }
    if (lines.empty()) {
        log::error("CNC", "Cannot start an empty stream");
        return false;
    }
    if (m_streaming.load()) {
        log::error("CNC", "Cannot replace an active stream");
        return false;
    }
    if (m_errorState) {
        log::error("CNC", "Cannot start stream while in error state -- call acknowledgeError() first");
        if (m_mtq && m_callbacks.onError) {
            m_mtq->enqueue([cb = m_callbacks.onError]() {
                cb("Cannot start new job: previous streaming error must be acknowledged first");
            });
        }
        return false;
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
    return true;
}

void CncController::acknowledgeError() {
    m_errorState = false;
    log::info("CNC", "Streaming error acknowledged by operator");
}

void CncController::acknowledgeToolChange() {
    if (!m_toolChangePending.load()) return;
    m_toolChangePending = false;
    log::info("CNC", "Tool change acknowledged by operator -- resuming stream");
    std::lock_guard<std::mutex> lock(m_streamMutex);
    if (m_sendIndex < static_cast<int>(m_program.size())) ++m_sendIndex;
}

void CncController::stopStream() {
    m_streaming = false;
    postProgressUpdate();
    feedHold();
}

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
    m_errorState = false;
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        m_sentLengths.clear();
        m_bufferUsed = 0;
    }
    postProgressUpdate();
}

void CncController::setFeedOverride(int percent) {
    OverrideCmd cmd;
    cmd.bytes.push_back(cnc::CMD_FEED_100_PERCENT);
    int diff = percent - 100;
    while (diff >= 10)  { cmd.bytes.push_back(cnc::CMD_FEED_PLUS_10);  diff -= 10; }
    while (diff <= -10) { cmd.bytes.push_back(cnc::CMD_FEED_MINUS_10); diff += 10; }
    while (diff > 0)    { cmd.bytes.push_back(cnc::CMD_FEED_PLUS_1);   --diff; }
    while (diff < 0)    { cmd.bytes.push_back(cnc::CMD_FEED_MINUS_1);  ++diff; }
    std::lock_guard<std::mutex> lock(m_overrideMutex);
    m_pendingOverrides.push_back(std::move(cmd));
}

void CncController::setRapidOverride(int percent) {
    OverrideCmd cmd;
    cmd.bytes.push_back(percent <= 25 ? cnc::CMD_RAPID_25_PERCENT
                                     : percent <= 50 ? cnc::CMD_RAPID_50_PERCENT
                                                     : cnc::CMD_RAPID_100_PERCENT);
    std::lock_guard<std::mutex> lock(m_overrideMutex);
    m_pendingOverrides.push_back(std::move(cmd));
}

void CncController::setSpindleOverride(int percent) {
    OverrideCmd cmd;
    cmd.bytes.push_back(cnc::CMD_SPINDLE_100_PERCENT);
    int diff = percent - 100;
    while (diff >= 10)  { cmd.bytes.push_back(cnc::CMD_SPINDLE_PLUS_10);  diff -= 10; }
    while (diff <= -10) { cmd.bytes.push_back(cnc::CMD_SPINDLE_MINUS_10); diff += 10; }
    while (diff > 0)    { cmd.bytes.push_back(cnc::CMD_SPINDLE_PLUS_1);   --diff; }
    while (diff < 0)    { cmd.bytes.push_back(cnc::CMD_SPINDLE_MINUS_1);  ++diff; }
    std::lock_guard<std::mutex> lock(m_overrideMutex);
    m_pendingOverrides.push_back(std::move(cmd));
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
    StreamProgress progress;
    progress.totalLines = static_cast<int>(m_program.size());
    progress.ackedLines = m_ackIndex;
    progress.errorCount = m_errorCount;
    progress.streaming = m_streaming.load();
    const auto now = std::chrono::steady_clock::now();
    progress.elapsedSeconds =
        std::chrono::duration<f32>(now - m_streamStartTime).count();
    return progress;
}

void CncController::postProgressUpdate() {
    if (!m_mtq || !m_callbacks.onProgressUpdate) return;
    const auto progress = streamProgress();
    m_mtq->enqueue([cb = m_callbacks.onProgressUpdate, progress]() {
        cb(progress);
    });
}

} // namespace dw

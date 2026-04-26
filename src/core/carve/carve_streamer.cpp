#include "carve_streamer.h"

#include "../cnc/cnc_controller.h"

#include <cstdio>

namespace dw {
namespace carve {

namespace {

// Format float with minimal trailing zeros (consistent with gcode_export.cpp)
std::string fmt(f32 v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(v));
    std::string s(buf);
    auto dot = s.find('.');
    if (dot != std::string::npos) {
        auto last = s.find_last_not_of('0');
        if (last == dot) {
            s.erase(dot + 2); // Keep at least one decimal
        } else {
            s.erase(last + 1);
        }
    }
    return s;
}

} // anonymous namespace

void CarveStreamer::setCncController(CncController* cnc)
{
    m_cnc = cnc;
}

void CarveStreamer::start(const MultiPassToolpath& toolpath,
                          const ToolpathConfig& config,
                          cnc::SendUnits units)
{
    m_toolpath = toolpath;
    m_config = config;
    m_sendUnits = units;
    m_pointIndex = 0;
    m_lineNumber = 0;
    m_lastFeedRate = -1.0f;
    m_hasLastPosition = false;
    m_lastPosition = Vec3{0.0f};
    m_lastRapid = true;
    m_aborted.store(false, std::memory_order_release);
    m_paused.store(false, std::memory_order_release);

    // Compute total lines: preamble(1) + clearing + optional tool-change
    // boundary + finishing + postamble(3: retract + M5 + M30).
    int clearingCount = static_cast<int>(toolpath.clearing.points.size());
    int finishingCount = static_cast<int>(toolpath.finishing.points.size());
    const int toolChangeCount =
        (toolpath.requiresToolChange && clearingCount > 0 && finishingCount > 0)
            ? 6
            : 0;
    m_totalLines = 1 + clearingCount + toolChangeCount + finishingCount + 3;

    // Determine starting phase
    if (clearingCount > 0) {
        m_phase = Phase::Preamble;
    } else if (finishingCount > 0) {
        m_phase = Phase::Preamble;
    } else {
        m_phase = Phase::Complete;
        m_totalLines = 0;
        m_running.store(false, std::memory_order_release);
        return;
    }

    m_running.store(true, std::memory_order_release);
}

std::string CarveStreamer::nextLine()
{
    if (m_aborted.load(std::memory_order_acquire)) {
        return {};
    }

    if (m_paused.load(std::memory_order_acquire)) {
        return {};
    }

    if (m_phase == Phase::Complete) {
        return {};
    }

    // Preamble: absolute distance mode plus detected send units.
    if (m_phase == Phase::Preamble) {
        m_lineNumber++;
        // Determine next phase after preamble
        if (!m_toolpath.clearing.points.empty()) {
            m_phase = Phase::Clearing;
        } else {
            m_phase = Phase::Finishing;
        }
        m_pointIndex = 0;
        return preamble();
    }

    // Clearing pass
    if (m_phase == Phase::Clearing) {
        if (m_pointIndex < m_toolpath.clearing.points.size()) {
            const auto& pt = m_toolpath.clearing.points[m_pointIndex];
            m_pointIndex++;
            m_lineNumber++;
            if (pt.rapid) {
                auto line = formatRapid(pt.position);
                rememberMove(pt.position, true);
                return line;
            }
            auto line = formatLinear(pt.position, feedRateForLinearMove(pt.position));
            rememberMove(pt.position, false);
            return line;
        }
        // Clearing complete, switch to tool-change boundary if needed.
        if (m_toolpath.requiresToolChange &&
            !m_toolpath.finishing.points.empty()) {
            m_phase = Phase::ToolChange;
        } else {
            m_phase = Phase::Finishing;
        }
        m_pointIndex = 0;
    }

    if (m_phase == Phase::ToolChange) {
        const std::string finishTool = m_toolpath.finishingToolName.empty()
            ? "finish tool"
            : m_toolpath.finishingToolName;
        const std::string roughTool = m_toolpath.clearingToolName.empty()
            ? "roughing tool"
            : m_toolpath.clearingToolName;

        std::string line;
        if (m_pointIndex == 0) {
            line = "G0 Z" + fmt(cnc::toSendLength(m_config.safeZMm, m_sendUnits));
        } else if (m_pointIndex == 1) {
            line = "M5";
        } else if (m_pointIndex == 2) {
            line = "(Tool change: " + roughTool + " -> " + finishTool + ")";
        } else if (m_pointIndex == 3) {
            line = "(Install finish tool: " + finishTool + ")";
        } else if (m_pointIndex == 4) {
            line = "(Re-zero Z with finish tool before continuing)";
        } else {
            line = "M0";
            m_phase = Phase::Finishing;
            m_lastFeedRate = -1.0f;
            m_hasLastPosition = false;
            m_lastPosition = Vec3{0.0f};
            m_lastRapid = true;
        }

        m_pointIndex++;
        m_lineNumber++;
        if (m_phase == Phase::Finishing) {
            m_pointIndex = 0;
        }
        return line;
    }

    // Finishing pass
    if (m_phase == Phase::Finishing) {
        if (m_pointIndex < m_toolpath.finishing.points.size()) {
            const auto& pt = m_toolpath.finishing.points[m_pointIndex];
            m_pointIndex++;
            m_lineNumber++;
            if (pt.rapid) {
                auto line = formatRapid(pt.position);
                rememberMove(pt.position, true);
                return line;
            }
            auto line = formatLinear(pt.position, feedRateForLinearMove(pt.position));
            rememberMove(pt.position, false);
            return line;
        }
        // Finishing complete, emit postamble
        m_phase = Phase::Postamble;
        m_pointIndex = 0;
    }

    // Postamble: 3 lines (retract, spindle stop, program end)
    if (m_phase == Phase::Postamble) {
        std::string line;
        if (m_pointIndex == 0) {
            line = "G0 Z" + fmt(cnc::toSendLength(m_config.safeZMm, m_sendUnits));
        } else if (m_pointIndex == 1) {
            line = "M5";
        } else {
            line = "M30";
            m_phase = Phase::Complete;
            m_running.store(false, std::memory_order_release);
        }
        m_pointIndex++;
        m_lineNumber++;
        return line;
    }

    return {};
}

void CarveStreamer::pause()
{
    m_paused.store(true, std::memory_order_release);
    if (m_cnc) {
        m_cnc->feedHold();
    }
}

void CarveStreamer::resume()
{
    m_paused.store(false, std::memory_order_release);
    if (m_cnc) {
        m_cnc->cycleStart();
    }
}

void CarveStreamer::abort()
{
    m_aborted.store(true, std::memory_order_release);
    m_running.store(false, std::memory_order_release);
    m_phase = Phase::Complete;
    if (m_cnc) {
        m_cnc->softReset();
    }
}

bool CarveStreamer::isRunning() const
{
    return m_running.load(std::memory_order_acquire);
}

bool CarveStreamer::isPaused() const
{
    return m_paused.load(std::memory_order_acquire);
}

bool CarveStreamer::isComplete() const
{
    return m_phase == Phase::Complete;
}

int CarveStreamer::currentLine() const
{
    return m_lineNumber;
}

int CarveStreamer::totalLines() const
{
    return m_totalLines;
}

f32 CarveStreamer::progressFraction() const
{
    if (m_totalLines <= 0) return 1.0f;
    return static_cast<f32>(m_lineNumber) / static_cast<f32>(m_totalLines);
}

std::string CarveStreamer::formatRapid(const Vec3& pos) const
{
    return "G0 X" + fmt(cnc::toSendLength(pos.x, m_sendUnits)) +
           " Y" + fmt(cnc::toSendLength(pos.y, m_sendUnits)) +
           " Z" + fmt(cnc::toSendLength(pos.z, m_sendUnits));
}

std::string CarveStreamer::formatLinear(const Vec3& pos, f32 feedRate)
{
    const f32 sendFeed = cnc::toSendFeed(feedRate, m_sendUnits);
    std::string line = "G1 X" + fmt(cnc::toSendLength(pos.x, m_sendUnits)) +
                       " Y" + fmt(cnc::toSendLength(pos.y, m_sendUnits)) +
                       " Z" + fmt(cnc::toSendLength(pos.z, m_sendUnits));
    if (sendFeed != m_lastFeedRate) {
        line += " F" + fmt(sendFeed);
        m_lastFeedRate = sendFeed;
    }
    return line;
}

f32 CarveStreamer::feedRateForLinearMove(const Vec3& pos) const
{
    if (!m_hasLastPosition) {
        return m_config.feedRateMmMin;
    }
    return linearMoveFeedRateMmMin(m_lastPosition, m_lastRapid, pos, m_config);
}

void CarveStreamer::rememberMove(const Vec3& pos, bool rapid)
{
    m_lastPosition = pos;
    m_lastRapid = rapid;
    m_hasLastPosition = true;
}

std::string CarveStreamer::preamble() const
{
    return std::string("G90 ") + cnc::gcodeUnitMode(m_sendUnits);
}

std::string CarveStreamer::postamble() const
{
    return "G0 Z" + fmt(cnc::toSendLength(m_config.safeZMm, m_sendUnits)) + "\nM5\nM30";
}

} // namespace carve
} // namespace dw

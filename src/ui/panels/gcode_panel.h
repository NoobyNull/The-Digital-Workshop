#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../../core/gcode/gcode_analyzer.h"
#include "../../core/gcode/gcode_parser.h"
#include "../../core/gcode/machine_profile.h"
#include "../../core/cnc/cnc_types.h"
#include "../../core/database/job_repository.h"
#include "panel.h"

namespace dw {

class FileDialog;
class GCodeRepository;
class ProjectManager;
class CncController;
class ToolDatabase;

// Panel mode — View (text listing), Send (real CNC)
enum class GCodePanelMode { View, Send };

// Console line entry
struct ConsoleLine {
    std::string text;
    enum Type { Sent, Ok, Error, Status, Info } type = Info;
};

// G-code text listing / statistics / sender panel
class GCodePanel : public Panel {
  public:
    GCodePanel();
    ~GCodePanel() override = default;

    void render() override;

    // Dependency injection
    void setFileDialog(FileDialog* dialog) { m_fileDialog = dialog; }
    void setGCodeRepository(GCodeRepository* repo) { m_gcodeRepo = repo; }
    void setProjectManager(ProjectManager* pm) { m_projectManager = pm; }
    void setCncController(CncController* ctrl) { m_cnc = ctrl; }
    void setJobRepository(JobRepository* repo) { m_jobRepo = repo; }
    void setToolDatabase(ToolDatabase* db) { m_toolDatabase = db; }
    void setOpenMachineProfilesCallback(std::function<void()> cb) {
        m_openMachineProfiles = std::move(cb);
    }

    // Callback notifications for program load/clear
    void setOnProgramLoaded(std::function<void(const gcode::Program&)> cb) { m_onProgramLoaded = std::move(cb); }
    void setOnProgramCleared(std::function<void()> cb) { m_onProgramCleared = std::move(cb); }
    void onMachineProfileChanged();

    // Load G-code from file
    bool loadFile(const std::string& path);
    void clear();
    bool hasGCode() const { return m_program.commands.size() > 0; }

    // Get raw G-code lines for resume-from-line feature
    std::vector<std::string> getRawLines() const;

    // Get toolpath bounds (from analyzed statistics)
    Vec3 boundsMin() const { return m_stats.boundsMin; }
    Vec3 boundsMax() const { return m_stats.boundsMax; }

    // Called by application_wiring to push GRBL events into the panel
    void onGrblConnected(bool connected, const std::string& version);
    void onGrblStatus(const MachineStatus& status);
    void onGrblLineAcked(const LineAck& ack);
    void onGrblProgress(const StreamProgress& progress);
    void onGrblAlarm(int code, const std::string& desc);
    void onGrblError(const std::string& message);
    void onGrblRawLine(const std::string& line, bool isSent);

    // Direct Carve streaming — called by DirectCarvePanel
    void onCarveStreamStart(int totalLines);
    void onCarveStreamProgress(int currentLine, int totalLines, float elapsedSec);
    void onCarveStreamComplete();
    void onCarveStreamAborted();

  private:
    // Render sections
    void renderToolbar();
    void renderRecentFiles();
    void renderModeTabs();
    void renderMachineProfileSelector();
    void renderStatistics();
    void renderConnectionBar();
    void renderMachineStatus();
    void renderPlaybackControls();
    void renderProgressBar();
    void renderFeedOverride();
    void renderConsole();
    void renderJobHistory();

    // Re-run analyzer with current profile
    void reanalyze();

    // Helpers
    void addConsoleLine(const std::string& text, ConsoleLine::Type type);
    void buildSendProgram();

    // Program load/clear callbacks
    std::function<void(const gcode::Program&)> m_onProgramLoaded;
    std::function<void()> m_onProgramCleared;

    FileDialog* m_fileDialog = nullptr;

    gcode::Program m_program;
    gcode::Statistics m_stats;
    std::string m_filePath;

    // Persistence
    GCodeRepository* m_gcodeRepo = nullptr;
    JobRepository* m_jobRepo = nullptr;
    ProjectManager* m_projectManager = nullptr;
    i64 m_currentGCodeId = -1;

    // Job history
    i64 m_activeJobId = -1;
    int m_lastProgressSave = 0; // Last acked line when progress was saved
    bool m_showJobHistory = false;
    std::vector<JobRecord> m_jobHistoryCache;
    bool m_jobHistoryDirty = true;

    // Mode
    GCodePanelMode m_mode = GCodePanelMode::View;

    // Tool database
    ToolDatabase* m_toolDatabase = nullptr;

    // --- Sender state ---
    CncController* m_cnc = nullptr;
    bool m_cncConnected = false;
    std::string m_cncVersion;
    MachineStatus m_machineStatus;
    StreamProgress m_streamProgress;
    int m_feedOverridePercent = 100;

    // Connection mode
    enum class ConnMode { Serial, Tcp };
    ConnMode m_connMode = ConnMode::Serial;

    // Serial port selection
    std::vector<std::string> m_availablePorts;
    size_t m_selectedPort = 0;
    size_t m_selectedBaud = 3; // Index into baud rate list (115200)
    static constexpr int BAUD_RATES[] = {9600, 19200, 38400, 57600, 115200, 230400};
    static constexpr int NUM_BAUD_RATES = 6;

    // TCP connection state
    char m_tcpHost[128] = "192.168.1.1";  // ImGui InputText buffer
    int m_tcpPort = 23;                    // Default telnet port (common for serial-to-ethernet bridges)

    // Console
    std::deque<ConsoleLine> m_consoleLines;
    bool m_consoleAutoScroll = true;
    bool m_consoleOpen = false;
    static constexpr size_t MAX_CONSOLE_LINES = 500;

    // Progress highlighting — which line index was last acked
    int m_lastAckedLine = -1;

    // Job completion flash timer
    float m_jobFlashTimer = 0.0f;

    // Global machine profile editor opener
    std::function<void()> m_openMachineProfiles;

    // G-code search/goto (EXT-12)
    char m_searchBuf[128] = "";
    int m_searchResultLine = -1;
    bool m_searchActive = false;
    int m_gotoLine = 0;
    int m_scrollToLine = -1;

    void renderSearchBar();
    void findNext();
    void gotoLineNumber(int lineNum);

    // Direct Carve streaming state
    void renderCarveProgress();
    bool m_carveStreamActive = false;
    int m_carveCurrentLine = 0;
    int m_carveTotalLines = 0;
    float m_carveElapsedSec = 0.0f;
    bool m_carveAborted = false;
};

} // namespace dw

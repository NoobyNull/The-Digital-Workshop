#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../../core/cnc/cnc_types.h"
#include "../../core/cnc/unified_settings.h"
#include "panel.h"

namespace dw {

class CncController;
class FileDialog;

// CNC firmware settings panel -- unified view for GRBL, grblHAL, and FluidNC.
// Receives raw lines via onRawLine callback to capture $$ / $S responses.
// Provides category-based settings view with optional Advanced mode showing raw IDs.
class CncSettingsPanel : public Panel {
  public:
    CncSettingsPanel();
    ~CncSettingsPanel() override = default;

    void render() override;

    // Dependencies
    void setCncController(CncController* cnc) { m_cnc = cnc; }
    void setFileDialog(FileDialog* fd) { m_fileDialog = fd; }
    void setOpenMachineProfilesCallback(std::function<void()> cb) {
        m_openMachineProfiles = std::move(cb);
    }

    // Callbacks (called on main thread via MainThreadQueue)
    void onConnectionChanged(bool connected, const std::string& version);
    void onRawLine(const std::string& line, bool isSent);
    void onStatusUpdate(const MachineStatus& status);

    // Access settings for profile sync
    const UnifiedSettingsMap& unifiedSettings() const { return m_settings; }
    bool hasSettings() const { return !m_settings.empty(); }

    // EditBuffer needs to be public for helper functions
    struct EditBuffer {
        std::string key;
        char buf[32] = "";
        bool active = false;
    };

  private:
    // Rendering sub-sections
    void renderMachineProfileSection();
    void renderToolbar();
    void renderSettingsTab();
    void renderTuningTab();
    void renderSafetyTab();
    void renderRawTab();
    void renderDiffDialog();

    // Unified setting rendering helpers
    void renderUnifiedBool(const std::string& key, const char* label,
                           const char* onLabel, const char* offLabel);
    void renderUnifiedBitmask(const std::string& key, const char* label);
    void renderUnifiedNumeric(const std::string& key, const char* label,
                              const char* units, float width);
    void renderUnifiedPerAxisGroup(const char* label, const char* units,
                                    const std::string& keyX,
                                    const std::string& keyY,
                                    const std::string& keyZ);
    void renderAdvancedId(const UnifiedSetting& s);

    // Actions
    void requestSettings();
    void applySetting(const std::string& key, const std::string& value);
    void writeAllModified();
    void saveToFlash();
    void backupToFile();
    void restoreFromFile();
    void exportPlainText();

    // State
    CncController* m_cnc = nullptr;
    FileDialog* m_fileDialog = nullptr;
    UnifiedSettingsMap m_settings;
    FirmwareType m_firmwareType = FirmwareType::GRBL;
    MachineState m_machineState = MachineState::Unknown;
    bool m_connected = false;
    bool m_collecting = false;    // True while collecting response lines
    bool m_collectingSC = false;  // True while collecting $SC response
    bool m_advancedView = false;  // Show raw firmware IDs
    bool m_locked = true;         // Prevent accidental setting changes

    // Editing state
    std::map<std::string, EditBuffer> m_editBuffers;

    // Diff dialog state (for restore preview)
    bool m_showDiffDialog = false;
    UnifiedSettingsMap m_restoreSettings;
    std::vector<UnifiedSettingsMap::DiffEntry> m_diffEntries;

    // Write queue for sequential EEPROM-safe writes (GRBL only)
    struct WriteQueueItem {
        std::string key;
        std::string value;
    };
    std::vector<WriteQueueItem> m_writeQueue;
    int m_writeIndex = 0;
    bool m_writing = false;
    float m_writeTimer = 0.0f;
    static constexpr float WRITE_DELAY_SEC = 0.05f; // 50ms between EEPROM writes

    // Global machine profile editor opener
    std::function<void()> m_openMachineProfiles;

    // Tab selection
    int m_activeTab = 0;

    // Firmware info ($I response)
    std::string m_firmwareInfo;
    bool m_requestingInfo = false;
};

} // namespace dw

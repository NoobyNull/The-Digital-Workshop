#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "dialog.h"

namespace dw {

// File filter for dialogs
struct FileFilter {
    std::string name;       // e.g., "3D Models"
    std::string extensions; // e.g., "*.stl;*.obj;*.3mf"
};

// File dialog mode
enum class FileDialogMode { Open, Save, SelectFolder };

// Simple file dialog (native dialogs not available, using ImGui)
class FileDialog : public Dialog {
  public:
    FileDialog();
    ~FileDialog() override = default;

    void render() override;

    // Show open file dialog (single file)
    void showOpen(const std::string& title,
                  const std::vector<FileFilter>& filters,
                  std::function<void(const std::string&)> callback);

    // Show open file dialog (multi-select)
    void showOpenMulti(const std::string& title,
                       const std::vector<FileFilter>& filters,
                       std::function<void(const std::vector<std::string>&)> callback);

    // Show save file dialog
    void showSave(const std::string& title,
                  const std::vector<FileFilter>& filters,
                  const std::string& defaultName,
                  std::function<void(const std::string&)> callback);

    // Show folder selection dialog
    void showFolder(const std::string& title, std::function<void(const std::string&)> callback);

    // Native OS file dialog (supports network locations via GVFS/portal)
    void showNativeOpen(const std::string& title,
                        const std::vector<FileFilter>& filters,
                        std::function<void(const std::string&)> callback);

    void showNativeOpenMulti(const std::string& title,
                             const std::vector<FileFilter>& filters,
                             std::function<void(const std::vector<std::string>&)> callback);

    void showNativeSave(const std::string& title,
                        const std::vector<FileFilter>& filters,
                        const std::string& defaultName,
                        std::function<void(const std::string&)> callback);

    void showNativeFolder(const std::string& title,
                          std::function<void(const std::string&)> callback);

    // Common filters
    static std::vector<FileFilter> modelFilters();
    static std::vector<FileFilter> projectFilters();
    static std::vector<FileFilter> archiveFilters();
    static std::vector<FileFilter> gcodeFilters();
    static std::vector<FileFilter> allFilters();

  private:
    void refreshDirectory();
    bool matchesFilter(const std::string& filename) const;

    FileDialogMode m_mode = FileDialogMode::Open;
    bool m_multiSelect = false;
    std::string m_currentPath;
    std::string m_selectedFile;
    std::string m_inputFileName;
    std::vector<FileFilter> m_filters;
    int m_selectedFilter = 0;

    struct DirEntry {
        std::string name;
        bool isDirectory;
        uint64_t size;
    };
    std::vector<DirEntry> m_entries;
    std::set<std::string> m_selectedFiles; // Multi-select set

    std::function<void(const std::string&)> m_callback;
    std::function<void(const std::vector<std::string>&)> m_multiCallback;
};

} // namespace dw

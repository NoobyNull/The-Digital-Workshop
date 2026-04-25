#include "file_dialog.h"

#include <algorithm>

#include <imgui.h>
#include <nfd.h>

#include "../../core/utils/file_utils.h"
#include "../icons.h"

namespace dw {

FileDialog::FileDialog() : Dialog("File Dialog") {
    m_currentPath = file::homeDirectory().string();
}

void FileDialog::render() {
    if (!m_open) {
        return;
    }

    ImGui::OpenPopup(m_title.c_str());

    const auto* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.45f, viewport->WorkSize.y * 0.5f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(m_title.c_str(), &m_open)) {
        // Path bar
        ImGui::Text("%s", m_currentPath.c_str());
        float upBtnW = ImGui::CalcTextSize("Up").x + ImGui::GetStyle().FramePadding.x * 4;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - upBtnW);
        if (ImGui::Button("Up")) {
            std::string parent = file::getParent(m_currentPath).string();
            if (!parent.empty() && parent != m_currentPath) {
                m_currentPath = parent;
                refreshDirectory();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Home")) {
            m_currentPath = file::homeDirectory().string();
            refreshDirectory();
        }

        ImGui::Separator();

        // File list
        float bottomReserve = ImGui::GetFrameHeightWithSpacing() * 3; // filename input + filter + buttons
        ImGui::BeginChild("FileList", ImVec2(0, -bottomReserve), true);

        for (const auto& entry : m_entries) {
            bool isSelected = m_multiSelect ? (m_selectedFiles.count(entry.name) > 0)
                                            : (entry.name == m_selectedFile);
            std::string label;

            if (entry.isDirectory) {
                label = std::string(Icons::Folder) + " " + entry.name;
            } else {
                label = std::string(Icons::File) + " " + entry.name;
            }

            if (ImGui::Selectable(
                    label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (m_multiSelect) {
                    // Ctrl+click toggles selection for both files and directories
                    if (ImGui::GetIO().KeyCtrl) {
                        if (m_selectedFiles.count(entry.name) > 0) {
                            m_selectedFiles.erase(entry.name);
                        } else {
                            m_selectedFiles.insert(entry.name);
                        }
                    } else if (!entry.isDirectory || !ImGui::IsMouseDoubleClicked(0)) {
                        // Single-click on anything: select it
                        // (but don't clear selection on directory double-click,
                        //  since that navigates into the folder)
                        m_selectedFiles.clear();
                        m_selectedFiles.insert(entry.name);
                    }
                    m_selectedFile = entry.name;
                } else {
                    m_selectedFile = entry.name;
                    if (m_mode == FileDialogMode::Save) {
                        m_inputFileName = entry.name;
                    }
                }

                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (entry.isDirectory) {
                        m_currentPath = m_currentPath + "/" + entry.name;
                        m_selectedFile.clear();
                        m_selectedFiles.clear();
                        refreshDirectory();
                        break;
                    } else if (m_mode == FileDialogMode::Open && !m_multiSelect) {
                        std::string fullPath = m_currentPath + "/" + m_selectedFile;
                        m_open = false;
                        if (m_callback) {
                            m_callback(fullPath);
                        }
                    }
                }
            }
        }

        ImGui::EndChild();

        // Bottom section
        ImGui::Separator();

        if (m_mode == FileDialogMode::Save) {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.65f);
            ImGui::InputText("Filename", m_inputFileName.data(), 256);
        }

        // Filter dropdown
        if (!m_filters.empty() && m_mode != FileDialogMode::SelectFolder) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
            if (ImGui::BeginCombo("##Filter", m_filters[m_selectedFilter].name.c_str())) {
                for (size_t i = 0; i < m_filters.size(); ++i) {
                    if (ImGui::Selectable(m_filters[i].name.c_str(),
                                          i == static_cast<size_t>(m_selectedFilter))) {
                        m_selectedFilter = static_cast<int>(i);
                        refreshDirectory();
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Spacing();

        // Buttons
        float buttonWidth = ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 4;

        if (m_multiSelect && !m_selectedFiles.empty()) {
            ImGui::Text("%d item(s) selected", static_cast<int>(m_selectedFiles.size()));
            ImGui::SameLine();
        }

        if (m_mode == FileDialogMode::Open) {
            if (ImGui::Button("Open", ImVec2(buttonWidth, 0))) {
                m_open = false;
                if (m_multiSelect && m_multiCallback) {
                    std::vector<std::string> paths;
                    for (const auto& name : m_selectedFiles) {
                        paths.push_back(m_currentPath + "/" + name);
                    }
                    m_multiCallback(paths);
                } else if (!m_selectedFile.empty() && m_callback) {
                    m_callback(m_currentPath + "/" + m_selectedFile);
                }
            }
        } else if (m_mode == FileDialogMode::Save) {
            if (ImGui::Button("Save", ImVec2(buttonWidth, 0))) {
                if (!m_inputFileName.empty()) {
                    std::string fullPath = m_currentPath + "/" + m_inputFileName;
                    m_open = false;
                    if (m_callback) {
                        m_callback(fullPath);
                    }
                }
            }
        } else {
            if (ImGui::Button("Select", ImVec2(buttonWidth, 0))) {
                m_open = false;
                if (m_callback) {
                    m_callback(m_currentPath);
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
            m_open = false;
            if (m_multiSelect && m_multiCallback) {
                m_multiCallback({});
            } else if (m_callback) {
                m_callback("");
            }
        }

        ImGui::EndPopup();
    }
}

void FileDialog::showOpen(const std::string& title,
                          const std::vector<FileFilter>& filters,
                          std::function<void(const std::string&)> callback) {
    m_title = title;
    m_mode = FileDialogMode::Open;
    m_multiSelect = false;
    m_filters = filters;
    m_selectedFilter = 0;
    m_callback = std::move(callback);
    m_multiCallback = nullptr;
    m_selectedFile.clear();
    m_selectedFiles.clear();
    refreshDirectory();
    m_open = true;
}

void FileDialog::showOpenMulti(const std::string& title,
                               const std::vector<FileFilter>& filters,
                               std::function<void(const std::vector<std::string>&)> callback) {
    m_title = title;
    m_mode = FileDialogMode::Open;
    m_multiSelect = true;
    m_filters = filters;
    m_selectedFilter = 0;
    m_callback = nullptr;
    m_multiCallback = std::move(callback);
    m_selectedFile.clear();
    m_selectedFiles.clear();
    refreshDirectory();
    m_open = true;
}

void FileDialog::showSave(const std::string& title,
                          const std::vector<FileFilter>& filters,
                          const std::string& defaultName,
                          std::function<void(const std::string&)> callback) {
    m_title = title;
    m_mode = FileDialogMode::Save;
    m_filters = filters;
    m_selectedFilter = 0;
    m_inputFileName = defaultName;
    m_callback = std::move(callback);
    m_selectedFile.clear();
    refreshDirectory();
    m_open = true;
}

void FileDialog::showFolder(const std::string& title,
                            std::function<void(const std::string&)> callback) {
    m_title = title;
    m_mode = FileDialogMode::SelectFolder;
    m_filters.clear();
    m_callback = std::move(callback);
    m_selectedFile.clear();
    refreshDirectory();
    m_open = true;
}

void FileDialog::refreshDirectory() {
    m_entries.clear();

    auto names = file::listEntries(m_currentPath);

    for (const auto& name : names) {
        if (name.empty() || name[0] == '.') {
            continue; // Skip hidden files
        }

        std::string fullPath = m_currentPath + "/" + name;
        DirEntry entry;
        entry.name = name;
        entry.isDirectory = file::isDirectory(fullPath);
        entry.size = entry.isDirectory ? 0 : file::fileSize(fullPath);

        // Filter files
        if (!entry.isDirectory && m_mode != FileDialogMode::SelectFolder) {
            if (!matchesFilter(name)) {
                continue;
            }
        }

        m_entries.push_back(std::move(entry));
    }

    // Sort: directories first, then alphabetically
    std::sort(m_entries.begin(), m_entries.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory;
        }
        return a.name < b.name;
    });
}

bool FileDialog::matchesFilter(const std::string& filename) const {
    if (m_filters.empty() || m_selectedFilter >= static_cast<int>(m_filters.size())) {
        return true;
    }

    const auto& filter = m_filters[m_selectedFilter];
    if (filter.extensions == "*" || filter.extensions == "*.*") {
        return true;
    }

    // Parse extensions from filter (e.g., "*.stl;*.obj")
    std::string exts = filter.extensions;
    size_t pos = 0;
    while (pos < exts.size()) {
        size_t next = exts.find(';', pos);
        std::string pattern = exts.substr(pos, next - pos);

        // Remove leading "*."
        if (pattern.size() > 2 && pattern[0] == '*' && pattern[1] == '.') {
            std::string ext = pattern.substr(1); // ".stl"
            if (filename.size() >= ext.size()) {
                std::string fileExt = filename.substr(filename.size() - ext.size());
                // Case-insensitive comparison
                std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(), ::tolower);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (fileExt == ext) {
                    return true;
                }
            }
        }

        if (next == std::string::npos) {
            break;
        }
        pos = next + 1;
    }

    return false;
}

// --- Native OS file dialog implementations ---

namespace {

// Convert our FileFilter format to NFD's filter format
std::vector<nfdfilteritem_t> toNfdFilters(const std::vector<FileFilter>& filters) {
    std::vector<nfdfilteritem_t> nfdFilters;
    for (const auto& f : filters) {
        if (f.extensions == "*" || f.extensions == "*.*")
            continue; // NFD handles "all files" automatically

        // Convert "*.stl;*.obj" → "stl,obj"
        std::string spec;
        std::string exts = f.extensions;
        size_t pos = 0;
        while (pos < exts.size()) {
            size_t next = exts.find(';', pos);
            std::string pattern = exts.substr(pos, next == std::string::npos ? next : next - pos);
            // Strip leading "*."
            size_t dot = pattern.find('.');
            if (dot != std::string::npos) {
                if (!spec.empty()) spec += ',';
                spec += pattern.substr(dot + 1);
            }
            if (next == std::string::npos) break;
            pos = next + 1;
        }
        nfdFilters.push_back({f.name.c_str(), spec.c_str()});
    }
    return nfdFilters;
}

} // anonymous namespace

void FileDialog::showNativeOpen(const std::string& /*title*/,
                                const std::vector<FileFilter>& filters,
                                std::function<void(const std::string&)> callback) {
    NFD_Init();

    auto nfdFilters = toNfdFilters(filters);
    nfdchar_t* outPath = nullptr;
    nfdresult_t result = NFD_OpenDialog(&outPath,
                                         nfdFilters.empty() ? nullptr : nfdFilters.data(),
                                         static_cast<nfdfiltersize_t>(nfdFilters.size()),
                                         nullptr);

    if (result == NFD_OKAY && outPath) {
        std::string path(outPath);
        NFD_FreePath(outPath);
        NFD_Quit();
        if (callback) callback(path);
    } else {
        if (result == NFD_ERROR) {
            fprintf(stderr, "Native file dialog error: %s\n", NFD_GetError());
        }
        NFD_Quit();
        if (callback) callback("");
    }
}

void FileDialog::showNativeOpenMulti(const std::string& /*title*/,
                                     const std::vector<FileFilter>& filters,
                                     std::function<void(const std::vector<std::string>&)> callback) {
    NFD_Init();

    auto nfdFilters = toNfdFilters(filters);
    const nfdpathset_t* outPaths = nullptr;
    nfdresult_t result = NFD_OpenDialogMultiple(&outPaths,
                                                 nfdFilters.empty() ? nullptr : nfdFilters.data(),
                                                 static_cast<nfdfiltersize_t>(nfdFilters.size()),
                                                 nullptr);

    if (result == NFD_OKAY && outPaths) {
        std::vector<std::string> paths;
        nfdpathsetsize_t count = 0;
        NFD_PathSet_GetCount(outPaths, &count);
        for (nfdpathsetsize_t i = 0; i < count; ++i) {
            nfdchar_t* path = nullptr;
            if (NFD_PathSet_GetPath(outPaths, i, &path) == NFD_OKAY && path) {
                paths.emplace_back(path);
                NFD_PathSet_FreePath(path);
            }
        }
        NFD_PathSet_Free(outPaths);
        NFD_Quit();
        if (callback) callback(paths);
    } else {
        if (result == NFD_ERROR) {
            fprintf(stderr, "Native file dialog error: %s\n", NFD_GetError());
        }
        NFD_Quit();
        if (callback) callback({});
    }
}

void FileDialog::showNativeSave(const std::string& /*title*/,
                                const std::vector<FileFilter>& filters,
                                const std::string& defaultName,
                                std::function<void(const std::string&)> callback) {
    NFD_Init();

    auto nfdFilters = toNfdFilters(filters);
    nfdchar_t* outPath = nullptr;
    nfdresult_t result = NFD_SaveDialog(&outPath,
                                         nfdFilters.empty() ? nullptr : nfdFilters.data(),
                                         static_cast<nfdfiltersize_t>(nfdFilters.size()),
                                         nullptr,
                                         defaultName.c_str());

    if (result == NFD_OKAY && outPath) {
        std::string path(outPath);
        NFD_FreePath(outPath);
        NFD_Quit();
        if (callback) callback(path);
    } else {
        if (result == NFD_ERROR) {
            fprintf(stderr, "Native file dialog error: %s\n", NFD_GetError());
        }
        NFD_Quit();
        if (callback) callback("");
    }
}

void FileDialog::showNativeFolder(const std::string& /*title*/,
                                  std::function<void(const std::string&)> callback) {
    NFD_Init();

    nfdchar_t* outPath = nullptr;
    nfdresult_t result = NFD_PickFolder(&outPath, nullptr);

    if (result == NFD_OKAY && outPath) {
        std::string path(outPath);
        NFD_FreePath(outPath);
        NFD_Quit();
        if (callback) callback(path);
    } else {
        if (result == NFD_ERROR) {
            fprintf(stderr, "Native file dialog error: %s\n", NFD_GetError());
        }
        NFD_Quit();
        if (callback) callback("");
    }
}

std::vector<FileFilter> FileDialog::modelFilters() {
    return {{"3D Models", "*.stl;*.obj;*.3mf"},
            {"STL Files", "*.stl"},
            {"OBJ Files", "*.obj"},
            {"3MF Files", "*.3mf"},
            {"All Files", "*.*"}};
}

std::vector<FileFilter> FileDialog::projectFilters() {
    return {{"Digital Workshop Projects", "*.dwproj"}, {"All Files", "*.*"}};
}

std::vector<FileFilter> FileDialog::archiveFilters() {
    return {{"Project Archives", "*.dwp"}, {"All Files", "*.*"}};
}

std::vector<FileFilter> FileDialog::gcodeFilters() {
    return {{"G-code Files", "*.gcode;*.nc;*.ngc;*.tap"}, {"All Files", "*.*"}};
}

std::vector<FileFilter> FileDialog::allFilters() {
    return {{"All Files", "*.*"}};
}

} // namespace dw

#include "file_utils.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "log.h"

namespace dw {

namespace {

// Helper for filesystem operations that return bool and log on failure.
// The callable receives a std::error_code& and performs the fs operation.
template <typename F>
bool fsOp(const char* opName, const Path& path, F&& fn) {
    std::error_code ec;
    fn(ec);
    if (ec) {
        log::errorf(
            "FileIO", "Failed to %s: %s (%s)", opName, path.string().c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

// Two-path variant for copy/move operations.
template <typename F>
bool fsOp2(const char* opName, const Path& from, const Path& to, F&& fn) {
    std::error_code ec;
    fn(ec);
    if (ec) {
        log::errorf("FileIO",
                    "Failed to %s %s to %s: %s",
                    opName,
                    from.string().c_str(),
                    to.string().c_str(),
                    ec.message().c_str());
        return false;
    }
    return true;
}

} // anonymous namespace

namespace file {

Result<std::string> readText(const Path& path) {
    std::ifstream file(path, std::ios::in);
    if (!file.is_open()) {
        log::errorf("FileIO", "Failed to open for reading: %s", path.string().c_str());
        return std::nullopt;
    }

    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

Result<ByteBuffer> readBinary(const Path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        log::errorf("FileIO", "Failed to open for reading: %s", path.string().c_str());
        return std::nullopt;
    }

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    ByteBuffer buffer(static_cast<usize>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size))) {
        log::errorf("FileIO", "Failed to read: %s", path.string().c_str());
        return std::nullopt;
    }

    return buffer;
}

bool writeText(const Path& path, std::string_view content) {
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        log::errorf("FileIO", "Failed to open for writing: %s", path.string().c_str());
        return false;
    }

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return file.good();
}

bool writeTextAtomic(const Path& path, std::string_view content) {
    const Path parent = path.parent_path();
    if (!parent.empty() && !createDirectories(parent))
        return false;

    static std::atomic<unsigned long long> sequence{0};
#ifdef _WIN32
    const auto processId = static_cast<unsigned long long>(GetCurrentProcessId());
#else
    const auto processId = static_cast<unsigned long long>(getpid());
#endif
    Path temporary = path;
    temporary += ".tmp-" + std::to_string(processId) + "-" + std::to_string(sequence.fetch_add(1));
    if (!writeText(temporary, content))
        return false;

#ifdef _WIN32
    if (!MoveFileExW(
            temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        (void)file::remove(temporary);
        log::errorf("FileIO", "Failed to atomically replace: %s", path.string().c_str());
        return false;
    }
#else
    const int temporaryFd = ::open(temporary.c_str(), O_RDONLY);
    if (temporaryFd < 0 || ::fsync(temporaryFd) != 0) {
        if (temporaryFd >= 0)
            ::close(temporaryFd);
        (void)file::remove(temporary);
        log::errorf("FileIO", "Failed to sync temporary file: %s", temporary.c_str());
        return false;
    }
    ::close(temporaryFd);

    std::error_code replaceError;
    std::filesystem::rename(temporary, path, replaceError);
    if (replaceError) {
        (void)file::remove(temporary);
        log::errorf("FileIO",
                    "Failed to atomically replace %s: %s",
                    path.c_str(),
                    replaceError.message().c_str());
        return false;
    }

    if (!parent.empty()) {
        int directoryFlags = O_RDONLY;
#ifdef O_DIRECTORY
        directoryFlags |= O_DIRECTORY;
#endif
        const int parentFd = ::open(parent.c_str(), directoryFlags);
        if (parentFd >= 0) {
            (void)::fsync(parentFd);
            ::close(parentFd);
        }
    }
#endif
    return true;
}

bool writeBinary(const Path& path, const ByteBuffer& data) {
    return writeBinary(path, data.data(), data.size());
}

bool writeBinary(const Path& path, const void* data, usize size) {
    std::ofstream file(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        log::errorf("FileIO", "Failed to open for writing: %s", path.string().c_str());
        return false;
    }

    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return file.good();
}

bool exists(const Path& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

bool isFile(const Path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

bool isDirectory(const Path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

bool createDirectory(const Path& path) {
    bool result = false;
    fsOp("create directory", path, [&](std::error_code& ec) {
        result = fs::create_directory(path, ec);
    });
    return result || file::exists(path);
}

bool createDirectories(const Path& path) {
    bool result = false;
    fsOp("create directories", path, [&](std::error_code& ec) {
        result = fs::create_directories(path, ec);
    });
    return result || file::exists(path);
}

bool touch(const Path& path) {
    auto parent = path.parent_path();
    if (!parent.empty() && !createDirectories(parent))
        return false;

    std::ofstream file(path, std::ios::app);
    if (!file.is_open()) {
        log::errorf("FileIO", "Failed to touch file: %s", path.string().c_str());
        return false;
    }
    return file.good();
}

bool remove(const Path& path) {
    bool result = false;
    fsOp("remove", path, [&](std::error_code& ec) { result = fs::remove(path, ec); });
    return result;
}

bool copy(const Path& from, const Path& to) {
    return fsOp2("copy", from, to, [&](std::error_code& ec) {
        fs::copy(from, to, fs::copy_options::overwrite_existing, ec);
    });
}

bool move(const Path& from, const Path& to) {
    // Try fast rename first (works on same filesystem)
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec)
        return true;

    // Rename failed (likely cross-filesystem) — fall back to copy + delete
    if (!file::copy(from, to))
        return false;

    // Only remove source after verified copy
    fs::remove(from, ec);
    if (ec) {
        log::warningf("FileIO",
                      "Moved file but failed to remove source: %s (%s)",
                      from.string().c_str(),
                      ec.message().c_str());
    }
    return true;
}

Result<u64> getFileSize(const Path& path) {
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec) {
        return std::nullopt;
    }
    return static_cast<u64>(size);
}

std::vector<Path> listFiles(const Path& directory) {
    std::vector<Path> files;
    std::error_code ec;

    if (!fs::is_directory(directory, ec)) {
        return files;
    }

    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }

    return files;
}

std::vector<Path> listFiles(const Path& directory, const std::string& extension) {
    std::vector<Path> files;
    std::error_code ec;

    if (!fs::is_directory(directory, ec)) {
        return files;
    }

    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (entry.is_regular_file()) {
            std::string ext = getExtension(entry.path());
            if (ext == extension) {
                files.push_back(entry.path());
            }
        }
    }

    return files;
}

std::string getExtension(const Path& path) {
    std::string ext = path.extension().string();
    if (!ext.empty() && ext[0] == '.') {
        ext = ext.substr(1);
    }
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return ext;
}

std::string getStem(const Path& path) {
    return path.stem().string();
}

Path getParent(const Path& path) {
    return path.parent_path();
}

Path currentDirectory() {
    std::error_code ec;
    return fs::current_path(ec);
}

Path homeDirectory() {
#ifdef _WIN32
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile) {
        return Path(userprofile);
    }
    const char* home = std::getenv("HOME");
    if (home) {
        return Path(home);
    }
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return Path(home);
    }
#endif
    return currentDirectory();
}

std::vector<std::string> listEntries(const Path& directory) {
    std::vector<std::string> entries;
    std::error_code ec;

    if (!fs::is_directory(directory, ec)) {
        return entries;
    }

    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        entries.push_back(entry.path().filename().string());
    }

    return entries;
}

u64 fileSize(const Path& path) {
    auto result = getFileSize(path);
    return result.value_or(0);
}

void openInFileManager(const Path& path) {
#ifdef _WIN32
    // Dynamically load ShellExecuteW to avoid including shellapi.h
    // (shellapi.h causes build issues with some Windows SDK versions)
    auto shell32 = LoadLibraryW(L"shell32.dll");
    if (shell32) {
        using ShellExecuteWFn = HINSTANCE(WINAPI*)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT);
        auto shellExecute =
            reinterpret_cast<ShellExecuteWFn>(GetProcAddress(shell32, "ShellExecuteW"));
        if (shellExecute) {
            shellExecute(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        FreeLibrary(shell32);
    }
#elif defined(__APPLE__)
    pid_t pid = fork();
    if (pid == 0) {
        execlp("open", "open", path.string().c_str(), nullptr);
        _exit(127);
    }
#else
    pid_t pid = fork();
    if (pid == 0) {
        execlp("xdg-open", "xdg-open", path.string().c_str(), nullptr);
        _exit(127);
    }
#endif
}

} // namespace file
} // namespace dw

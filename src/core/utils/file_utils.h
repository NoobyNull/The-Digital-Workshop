#pragma once

#include <string>
#include <vector>

#include "../types.h"

namespace dw {
namespace file {

// Read entire file to string
Result<std::string> readText(const Path& path);

// Read entire file to bytes
Result<ByteBuffer> readBinary(const Path& path);

// Write string to file
[[nodiscard]] bool writeText(const Path& path, std::string_view content);

// Write bytes to file
[[nodiscard]] bool writeBinary(const Path& path, const ByteBuffer& data);
[[nodiscard]] bool writeBinary(const Path& path, const void* data, usize size);

// File operations
bool exists(const Path& path);
bool isFile(const Path& path);
bool isDirectory(const Path& path);
[[nodiscard]] bool createDirectory(const Path& path);
[[nodiscard]] bool createDirectories(const Path& path);
[[nodiscard]] bool touch(const Path& path);
[[nodiscard]] bool remove(const Path& path);
[[nodiscard]] bool copy(const Path& from, const Path& to);
[[nodiscard]] bool move(const Path& from, const Path& to);

// Get file size in bytes
Result<u64> getFileSize(const Path& path);

// List files in directory
std::vector<Path> listFiles(const Path& directory);
std::vector<Path> listFiles(const Path& directory, const std::string& extension);

// Get file extension (lowercase, without dot)
std::string getExtension(const Path& path);

// Get filename without extension
std::string getStem(const Path& path);

// Get parent directory
Path getParent(const Path& path);

// Get current working directory
Path currentDirectory();

// Get home directory
Path homeDirectory();

// List all entries (files and directories) in a directory
std::vector<std::string> listEntries(const Path& directory);

// Get file size (convenience wrapper)
u64 fileSize(const Path& path);

// Open a path in the system file manager (no shell injection risk)
void openInFileManager(const Path& path);

} // namespace file
} // namespace dw

#pragma once

#include <string>
#include <vector>

#include "../types.h"

namespace dw {

// Manifest entry types for project.json
struct ProjectModelEntry {
    std::string filename;
    std::string hash;
};

struct ProjectHeightmapEntry {
    std::string filename;
    f32 resolutionMmPerPx = 0.0f;
};

struct ProjectGCodeEntry {
    std::string filename;
    std::string toolDescription;
};

// Manages an on-disk project directory with JSON manifest.
// Directory structure:
//   <root>/
//     project.json
//     models/
//     heightmaps/
//     gcode/
//     images/
class ProjectDirectory {
  public:
    ProjectDirectory() = default;

    // Create a new project directory with initial manifest
    bool create(const Path& root, const std::string& name, const std::string& description = "");

    // Open an existing project directory (reads project.json)
    bool open(const Path& root);

    // Read an existing manifest without repairing or otherwise mutating storage.
    // Use this for preflight checks that must remain write-free.
    bool inspect(const Path& root);

    // Save manifest to project.json
    bool save();

    // Metadata and manifest reset
    void setMetadata(const std::string& name, const std::string& description);
    void setProjectId(i64 projectId) { m_projectId = projectId; }
    void clearModels();
    void clearGCode();

    // File registration
    bool addModelFile(const Path& sourcePath);
    bool addModelFile(const Path& sourcePath, const std::string& fileHash);
    bool addGCodeFile(const Path& sourcePath, const std::string& toolDescription = "");
    void addHeightmap(const std::string& filename, f32 resolutionMmPerPx);
    void addGCode(const std::string& filename, const std::string& toolDescription);

    // Sanitize a raw model name into a directory-safe form
    static std::string sanitizeName(const std::string& raw);

    // Accessors
    const Path& root() const { return m_root; }
    Path modelsDir() const { return m_root / "models"; }
    Path heightmapsDir() const { return m_root / "heightmaps"; }
    Path gcodeDir() const { return m_root / "gcode"; }
    Path imagesDir() const { return m_root / "images"; }
    Path costingDir() const { return m_root / "costing"; }

    const std::string& name() const { return m_name; }
    const std::string& description() const { return m_description; }
    i64 projectId() const { return m_projectId; }

    const std::vector<ProjectModelEntry>& models() const { return m_models; }
    const std::vector<ProjectHeightmapEntry>& heightmaps() const { return m_heightmaps; }
    const std::vector<ProjectGCodeEntry>& gcodeFiles() const { return m_gcodeFiles; }

  private:
    bool load(const Path& root, bool restoreSubdirectories);
    bool createSubdirs();

    Path m_root;
    i64 m_projectId = 0;
    std::string m_name;
    std::string m_description;

    std::vector<ProjectModelEntry> m_models;
    std::vector<ProjectHeightmapEntry> m_heightmaps;
    std::vector<ProjectGCodeEntry> m_gcodeFiles;
};

} // namespace dw

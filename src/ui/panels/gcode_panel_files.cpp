#include "gcode_panel.h"

#include <algorithm>
#include <utility>

#include "../../core/config/config.h"
#include "../../core/database/gcode_repository.h"
#include "../../core/gcode/gcode_document.h"
#include "../../core/mesh/hash.h"
#include "../../core/paths/path_resolver.h"
#include "../../core/utils/file_utils.h"
#include "../widgets/toast.h"

namespace dw {

bool GCodePanel::loadFile(const std::string& path) {
    const Path requestedPath(path);
    const Path durablePath =
        PathResolver::durableLocation(requestedPath, PathCategory::GCode);
    const Path livePath = PathResolver::resolve(durablePath, PathCategory::GCode);
    const Path storedPath = PathResolver::makeStorable(livePath, PathCategory::GCode);

    auto content = file::readText(livePath);
    if (!content) {
        ToastManager::instance().show(ToastType::Error,
                                      "File Read Error",
                                      "Could not read G-code file: " + durablePath.string());
        return false;
    }

    auto document = gcode::prepareDocument(
        std::move(*content), Config::instance().getActiveMachineProfile());
    if (!document.hasCommands()) {
        ToastManager::instance().show(ToastType::Warning,
                                      "Empty G-code",
                                      "File contains no valid G-code commands");
        return false;
    }

    return activatePreparedFile(path,
                                std::move(document.program),
                                std::move(document.statistics));
}

bool GCodePanel::loadPreparedFile(
    const std::string& path,
    const gcode::PreparedDocument& document) {
    if (!document.hasCommands()) {
        ToastManager::instance().show(ToastType::Warning,
                                      "Empty G-code",
                                      "File contains no valid G-code commands");
        return false;
    }
    return activatePreparedFile(path, document.program, document.statistics);
}

bool GCodePanel::activatePreparedFile(const std::string& path,
                                      gcode::Program program,
                                      gcode::Statistics statistics) {
    const Path requestedPath(path);
    const Path durablePath =
        PathResolver::durableLocation(requestedPath, PathCategory::GCode);
    const Path livePath = PathResolver::resolve(durablePath, PathCategory::GCode);
    const Path storedPath = PathResolver::makeStorable(livePath, PathCategory::GCode);
    if (!file::isFile(livePath)) {
        ToastManager::instance().show(ToastType::Error,
                                      "File Read Error",
                                      "Could not read G-code file: " + durablePath.string());
        return false;
    }

    m_program = std::move(program);
    m_stats = std::move(statistics);

    // Keep the materialized path for I/O, but never persist a desktop-session
    // KIO bridge path.
    m_filePath = livePath.string();
    m_durableFilePath = storedPath.string();
    Config::instance().addRecentGCodeFile(storedPath);
    Config::instance().save();

    m_currentGCodeId = -1;
    if (m_gcodeRepo) {
        if (auto existing = m_gcodeRepo->findByHash(hash::computeFile(livePath))) {
            m_currentGCodeId = existing->id;
        } else if (auto byPath = m_gcodeRepo->findByPath(storedPath)) {
            m_currentGCodeId = byPath->id;
        } else {
            const auto stem = file::getStem(livePath);
            auto matches = m_gcodeRepo->findByName(stem);
            const auto exact = std::find_if(
                matches.begin(), matches.end(), [&stem](const GCodeRecord& record) {
                    return record.name == stem;
                });
            if (exact != matches.end())
                m_currentGCodeId = exact->id;
            else if (!matches.empty())
                m_currentGCodeId = matches.front().id;
        }
    }

    if (m_onProgramLoaded)
        m_onProgramLoaded(m_program, m_stats);
    return true;
}

void GCodePanel::clear() {
    m_program = gcode::Program{};
    m_stats = gcode::Statistics{};
    m_filePath.clear();
    m_durableFilePath.clear();
    m_currentGCodeId = -1;
    m_lastAckedLine = -1;
    m_streamProgress = {};

    if (m_onProgramCleared)
        m_onProgramCleared();
}

} // namespace dw

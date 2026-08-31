#include "project_resume_file_store.h"

#include <cstdint>
#include <limits>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/paths/app_paths.h"
#include "core/utils/file_utils.h"
#include "core/utils/log.h"

namespace dw {
namespace {

constexpr std::int64_t kResumeFileVersion = 1;
constexpr const char* kResumeFileName = "project-resume.json";

bool hasSupportedVersion(const nlohmann::json& document) {
    const auto version = document.find("version");
    if (version == document.end())
        return false;
    if (version->is_number_unsigned())
        return version->get<std::uint64_t>() == static_cast<std::uint64_t>(kResumeFileVersion);
    return version->is_number_integer() && version->get<std::int64_t>() == kResumeFileVersion;
}

std::optional<std::int64_t> positiveId(const nlohmann::json& value) {
    if (value.is_number_unsigned()) {
        const auto raw = value.get<std::uint64_t>();
        if (raw == 0 || raw > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return std::nullopt;
        return static_cast<std::int64_t>(raw);
    }
    if (!value.is_number_integer())
        return std::nullopt;
    const auto raw = value.get<std::int64_t>();
    return raw > 0 ? std::optional<std::int64_t>{raw} : std::nullopt;
}

} // namespace

ProjectResumeFileStore::ProjectResumeFileStore()
    : ProjectResumeFileStore(paths::getConfigDir() / kResumeFileName) {}

ProjectResumeFileStore::ProjectResumeFileStore(Path path) : m_path(std::move(path)) {}

workshop::ProjectResumeLoadResult ProjectResumeFileStore::load() const {
    if (m_path.empty() || !file::exists(m_path))
        return {workshop::ProjectResumeLoadStatus::Missing, std::nullopt};
    if (!file::isFile(m_path))
        return {workshop::ProjectResumeLoadStatus::Invalid, std::nullopt};

    const auto content = file::readText(m_path);
    if (!content)
        return {workshop::ProjectResumeLoadStatus::ReadFailed, std::nullopt};

    const auto document = nlohmann::json::parse(*content, nullptr, false);
    if (!document.is_object() || !hasSupportedVersion(document)) {
        log::warningf("ProjectResume", "Ignoring invalid resume file: %s", m_path.string().c_str());
        return {workshop::ProjectResumeLoadStatus::Invalid, std::nullopt};
    }

    const auto projectValue = document.find("project_id");
    if (projectValue == document.end())
        return {workshop::ProjectResumeLoadStatus::Invalid, std::nullopt};
    const auto projectId = positiveId(*projectValue);
    if (!projectId)
        return {workshop::ProjectResumeLoadStatus::Invalid, std::nullopt};

    workshop::ProjectResumeBookmark bookmark;
    bookmark.project = workshop::ProjectId(*projectId);

    const auto itemValue = document.find("project_item_id");
    if (itemValue != document.end() && !itemValue->is_null()) {
        const auto itemId = positiveId(*itemValue);
        if (!itemId)
            return {workshop::ProjectResumeLoadStatus::Invalid, std::nullopt};
        bookmark.item = workshop::ProjectItemId(*itemId);
    }
    return {workshop::ProjectResumeLoadStatus::Loaded, std::move(bookmark)};
}

bool ProjectResumeFileStore::save(const workshop::ProjectResumeBookmark& bookmark) {
    if (m_path.empty() || !bookmark.valid() || (bookmark.item && !bookmark.item->valid()))
        return false;

    nlohmann::json document = {
        {"version", kResumeFileVersion},
        {"project_id", bookmark.project.value},
        {"project_item_id",
         bookmark.item ? nlohmann::json(bookmark.item->value) : nlohmann::json(nullptr)},
    };
    return file::writeTextAtomic(m_path, document.dump(2) + "\n");
}

bool ProjectResumeFileStore::clear() {
    if (m_path.empty())
        return false;
    if (!file::exists(m_path))
        return true;
    return file::remove(m_path);
}

} // namespace dw

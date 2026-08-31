#pragma once

#include "modules/workshop/project_resume.h"

#include "core/types.h"

namespace dw {

// Atomic, versioned persistence for the optional restart-resume bookmark.
// The default location is app-owned configuration state; tests and other
// composition roots may inject a different path.
class ProjectResumeFileStore final : public workshop::ProjectResumeStore {
  public:
    ProjectResumeFileStore();
    explicit ProjectResumeFileStore(Path path);

    [[nodiscard]] workshop::ProjectResumeLoadResult load() const override;
    [[nodiscard]] bool save(const workshop::ProjectResumeBookmark& bookmark) override;
    [[nodiscard]] bool clear() override;

  private:
    Path m_path;
};

} // namespace dw

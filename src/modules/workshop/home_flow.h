#pragma once

#include <optional>
#include <string>
#include <variant>

namespace dw::workshop {

struct HomeSnapshot {
    bool namingProject = false;
    bool submittingProject = false;
    std::string projectName;
    std::string validationMessage;

    [[nodiscard]] bool canSubmitProject() const noexcept {
        return namingProject && !submittingProject && !projectName.empty() &&
               validationMessage.empty();
    }
};

struct BeginNamedProject {};

struct EditProjectName {
    std::string name;
};

struct SubmitNamedProject {};

struct CancelNamedProject {};

struct CompleteNamedProject {
    bool created = false;
    std::string failureMessage;
};

using HomeIntent = std::variant<BeginNamedProject,
                                EditProjectName,
                                SubmitNamedProject,
                                CancelNamedProject,
                                CompleteNamedProject>;

struct CreateNamedProjectRequest {
    std::string name;
};

enum class HomeTransitionStatus {
    Applied,
    Unchanged,
    Rejected,
    CreateRequested,
};

struct HomeTransition {
    HomeTransitionStatus status = HomeTransitionStatus::Unchanged;
    HomeSnapshot snapshot;
    std::optional<CreateNamedProjectRequest> createRequest;
};

class HomeFlow final {
  public:
    [[nodiscard]] const HomeSnapshot& snapshot() const noexcept;
    HomeTransition dispatch(const HomeIntent& intent);

  private:
    HomeTransition handle(const BeginNamedProject& intent);
    HomeTransition handle(const EditProjectName& intent);
    HomeTransition handle(const SubmitNamedProject& intent);
    HomeTransition handle(const CancelNamedProject& intent);
    HomeTransition handle(const CompleteNamedProject& intent);
    [[nodiscard]] HomeTransition transition(HomeTransitionStatus status) const;

    HomeSnapshot m_snapshot;
};

} // namespace dw::workshop

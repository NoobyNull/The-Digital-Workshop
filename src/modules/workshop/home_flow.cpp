#include "home_flow.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace dw::workshop {
namespace {

constexpr std::size_t kMaximumProjectNameLength = 96;

std::string trim(std::string_view value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
                          return std::isspace(c) != 0;
                      }).base();
    if (first >= last)
        return {};
    return std::string(first, last);
}

std::string validate(std::string_view rawName) {
    const std::string name = trim(rawName);
    if (name.empty())
        return "Enter a project name.";
    if (name == "." || name == "..")
        return "Choose a descriptive project name.";
    if (name.size() > kMaximumProjectNameLength)
        return "Project names can be at most 96 characters.";
    for (const char raw : name) {
        const auto c = static_cast<unsigned char>(raw);
        if (c < 0x20 || c == 0x7f)
            return "Project names cannot contain control characters.";
        if (c == '/' || c == '\\')
            return "Project names cannot contain path separators.";
    }
    return {};
}

} // namespace

const HomeSnapshot& HomeFlow::snapshot() const noexcept {
    return m_snapshot;
}

HomeTransition HomeFlow::dispatch(const HomeIntent& intent) {
    return std::visit([this](const auto& value) { return handle(value); }, intent);
}

HomeTransition HomeFlow::handle(const BeginNamedProject&) {
    if (m_snapshot.namingProject)
        return transition(HomeTransitionStatus::Unchanged);
    m_snapshot = HomeSnapshot{};
    m_snapshot.namingProject = true;
    return transition(HomeTransitionStatus::Applied);
}

HomeTransition HomeFlow::handle(const EditProjectName& intent) {
    if (!m_snapshot.namingProject || m_snapshot.submittingProject)
        return transition(HomeTransitionStatus::Rejected);
    m_snapshot.projectName = intent.name;
    m_snapshot.validationMessage = validate(m_snapshot.projectName);
    return transition(HomeTransitionStatus::Applied);
}

HomeTransition HomeFlow::handle(const SubmitNamedProject&) {
    if (!m_snapshot.namingProject || m_snapshot.submittingProject)
        return transition(HomeTransitionStatus::Rejected);

    m_snapshot.projectName = trim(m_snapshot.projectName);
    m_snapshot.validationMessage = validate(m_snapshot.projectName);
    if (!m_snapshot.validationMessage.empty())
        return transition(HomeTransitionStatus::Rejected);

    m_snapshot.submittingProject = true;
    auto result = transition(HomeTransitionStatus::CreateRequested);
    result.createRequest = CreateNamedProjectRequest{m_snapshot.projectName};
    return result;
}

HomeTransition HomeFlow::handle(const CancelNamedProject&) {
    if (!m_snapshot.namingProject || m_snapshot.submittingProject)
        return transition(HomeTransitionStatus::Rejected);
    m_snapshot = HomeSnapshot{};
    return transition(HomeTransitionStatus::Applied);
}

HomeTransition HomeFlow::handle(const CompleteNamedProject& intent) {
    if (!m_snapshot.namingProject || !m_snapshot.submittingProject)
        return transition(HomeTransitionStatus::Rejected);
    if (intent.created) {
        m_snapshot = HomeSnapshot{};
    } else {
        m_snapshot.submittingProject = false;
        m_snapshot.validationMessage = intent.failureMessage.empty()
                                                   ? "The project could not be created. Try again."
                                                   : intent.failureMessage;
    }
    return transition(HomeTransitionStatus::Applied);
}

HomeTransition HomeFlow::transition(HomeTransitionStatus status) const {
    return HomeTransition{status, m_snapshot, std::nullopt};
}

} // namespace dw::workshop

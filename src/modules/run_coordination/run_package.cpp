#include "run_package.h"

#include <algorithm>
#include <utility>

namespace dw::run_coordination {
namespace {

bool validPreparationPin(const carve_preparation::PrepareCarvePin& pin) noexcept {
    return pin.project().valid() && pin.modelItem().valid() &&
           pin.modelItem().project == pin.project() && pin.modelSource().valid() &&
           pin.modelSource().kind == workshop::LibraryItemKind::Model &&
           pin.operationItem().valid() && pin.operationItem().project == pin.project() &&
           pin.operationItem().item != pin.modelItem().item && pin.token().valid();
}

} // namespace

ToolpathIdentity::ToolpathIdentity(workshop::ProjectItemRef operationItem,
                                   workshop::ProjectItemRef gcodeItem,
                                   ToolpathRevision revision,
                                   std::string contentFingerprint)
    : m_operationItem(operationItem), m_gcodeItem(gcodeItem), m_revision(revision),
      m_contentFingerprint(std::move(contentFingerprint)) {}

workshop::ProjectItemRef ToolpathIdentity::operationItem() const noexcept {
    return m_operationItem;
}

workshop::ProjectItemRef ToolpathIdentity::gcodeItem() const noexcept {
    return m_gcodeItem;
}

ToolpathRevision ToolpathIdentity::revision() const noexcept {
    return m_revision;
}

const std::string& ToolpathIdentity::contentFingerprint() const noexcept {
    return m_contentFingerprint;
}

bool ToolpathIdentity::valid() const noexcept {
    return m_operationItem.valid() && m_gcodeItem.valid() &&
           m_operationItem.project == m_gcodeItem.project &&
           m_operationItem.item != m_gcodeItem.item && m_revision.valid() &&
           !m_contentFingerprint.empty();
}

bool operator==(const ToolpathIdentity& lhs,
                const ToolpathIdentity& rhs) noexcept {
    return lhs.operationItem() == rhs.operationItem() &&
           lhs.gcodeItem() == rhs.gcodeItem() && lhs.revision() == rhs.revision() &&
           lhs.contentFingerprint() == rhs.contentFingerprint();
}

RunSetupIdentity::RunSetupIdentity(carve_preparation::PrepareCarvePin preparation,
                                   ToolpathIdentity toolpath)
    : m_preparation(std::move(preparation)), m_toolpath(std::move(toolpath)) {}

const carve_preparation::PrepareCarvePin&
RunSetupIdentity::preparation() const noexcept {
    return m_preparation;
}

const ToolpathIdentity& RunSetupIdentity::toolpath() const noexcept {
    return m_toolpath;
}

bool RunSetupIdentity::valid() const noexcept {
    return validPreparationPin(m_preparation) && m_toolpath.valid() &&
           m_toolpath.operationItem() == m_preparation.operationItem() &&
           m_toolpath.gcodeItem().project == m_preparation.project();
}

bool operator==(const RunSetupIdentity& lhs,
                const RunSetupIdentity& rhs) noexcept {
    return lhs.preparation() == rhs.preparation() && lhs.toolpath() == rhs.toolpath();
}

const std::array<RunPreflightCheck, 7>& requiredRunPreflightChecks() noexcept {
    static constexpr std::array<RunPreflightCheck, 7> checks = {
        RunPreflightCheck::MachineConnected,
        RunPreflightCheck::ControllerIdle,
        RunPreflightCheck::Homed,
        RunPreflightCheck::WorkZeroSet,
        RunPreflightCheck::ToolConfirmed,
        RunPreflightCheck::StockSecured,
        RunPreflightCheck::OutlineConfirmed,
    };
    return checks;
}

RunPreflightFacts::RunPreflightFacts(std::array<bool, 7> satisfied) noexcept
    : m_satisfied(satisfied) {}

RunPreflightFacts RunPreflightFacts::allSatisfied() noexcept {
    return RunPreflightFacts{{true, true, true, true, true, true, true}};
}

bool RunPreflightFacts::satisfied(RunPreflightCheck check) const noexcept {
    const auto index = static_cast<std::size_t>(check);
    return index < m_satisfied.size() && m_satisfied[index];
}

bool RunPreflightFacts::passed() const noexcept {
    return std::all_of(m_satisfied.begin(), m_satisfied.end(), [](bool value) {
        return value;
    });
}

std::vector<RunPreflightCheck> RunPreflightFacts::missingChecks() const {
    std::vector<RunPreflightCheck> missing;
    for (const auto check : requiredRunPreflightChecks()) {
        if (!satisfied(check))
            missing.push_back(check);
    }
    return missing;
}

bool operator==(const RunPreflightFacts& lhs,
                const RunPreflightFacts& rhs) noexcept {
    return lhs.m_satisfied == rhs.m_satisfied;
}

RunPreflightSnapshot::RunPreflightSnapshot(RunSetupIdentity setup,
                                           PreflightRevision revision,
                                           RunPreflightFacts facts)
    : m_setup(std::move(setup)), m_revision(revision), m_facts(std::move(facts)) {}

const RunSetupIdentity& RunPreflightSnapshot::setup() const noexcept {
    return m_setup;
}

PreflightRevision RunPreflightSnapshot::revision() const noexcept {
    return m_revision;
}

const RunPreflightFacts& RunPreflightSnapshot::facts() const noexcept {
    return m_facts;
}

bool RunPreflightSnapshot::passed() const noexcept {
    return m_revision.valid() && m_facts.passed();
}

bool operator==(const RunPreflightSnapshot& lhs,
                const RunPreflightSnapshot& rhs) noexcept {
    return lhs.setup() == rhs.setup() && lhs.revision() == rhs.revision() &&
           lhs.facts() == rhs.facts();
}

RunIdentity::RunIdentity(workshop::RunId run, RunSetupIdentity setup)
    : m_run(run), m_setup(std::move(setup)) {}

workshop::RunId RunIdentity::run() const noexcept {
    return m_run;
}

const RunSetupIdentity& RunIdentity::setup() const noexcept {
    return m_setup;
}

bool RunIdentity::valid() const noexcept {
    return m_run.valid() && m_setup.valid();
}

bool operator==(const RunIdentity& lhs, const RunIdentity& rhs) noexcept {
    return lhs.run() == rhs.run() && lhs.setup() == rhs.setup();
}

RunPackage::RunPackage(RunIdentity identity, RunPreflightSnapshot preflight)
    : m_identity(std::move(identity)), m_preflight(std::move(preflight)) {}

const RunIdentity& RunPackage::identity() const noexcept {
    return m_identity;
}

const RunPreflightSnapshot& RunPackage::preflight() const noexcept {
    return m_preflight;
}

RunPackageIssue RunPackage::issue() const noexcept {
    if (!m_identity.run().valid())
        return RunPackageIssue::InvalidRunId;

    const auto& preparation = m_identity.setup().preparation();
    if (!validPreparationPin(preparation))
        return RunPackageIssue::InvalidPreparationIdentity;
    if (!m_identity.setup().toolpath().valid())
        return RunPackageIssue::InvalidToolpathIdentity;
    if (!m_identity.setup().valid())
        return RunPackageIssue::SetupIdentityMismatch;
    if (!m_preflight.revision().valid())
        return RunPackageIssue::InvalidPreflightRevision;
    if (!(m_preflight.setup() == m_identity.setup()))
        return RunPackageIssue::PreflightBindingMismatch;
    if (!m_preflight.facts().passed())
        return RunPackageIssue::PreflightFailed;
    return RunPackageIssue::None;
}

bool RunPackage::valid() const noexcept {
    return issue() == RunPackageIssue::None;
}

bool operator==(const RunPackage& lhs, const RunPackage& rhs) noexcept {
    return lhs.identity() == rhs.identity() && lhs.preflight() == rhs.preflight();
}

} // namespace dw::run_coordination

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "modules/carve_preparation/preparation_identity.h"

namespace dw::run_coordination {

struct ToolpathRevision {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return value > 0; }

    friend constexpr bool operator==(ToolpathRevision lhs,
                                     ToolpathRevision rhs) noexcept {
        return lhs.value == rhs.value;
    }
};

struct PreflightRevision {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return value > 0; }

    friend constexpr bool operator==(PreflightRevision lhs,
                                     PreflightRevision rhs) noexcept {
        return lhs.value == rhs.value;
    }
};

// This identifies both the persisted G-code item and the operation/toolpath
// revision from which its exact bytes were produced. The fingerprint is owned
// by the adapter (normally a content hash); this module never reads a file.
class ToolpathIdentity final {
  public:
    ToolpathIdentity(workshop::ProjectItemRef operationItem,
                     workshop::ProjectItemRef gcodeItem,
                     ToolpathRevision revision,
                     std::string contentFingerprint);

    [[nodiscard]] workshop::ProjectItemRef operationItem() const noexcept;
    [[nodiscard]] workshop::ProjectItemRef gcodeItem() const noexcept;
    [[nodiscard]] ToolpathRevision revision() const noexcept;
    [[nodiscard]] const std::string& contentFingerprint() const noexcept;
    [[nodiscard]] bool valid() const noexcept;

    friend bool operator==(const ToolpathIdentity& lhs,
                           const ToolpathIdentity& rhs) noexcept;

  private:
    workshop::ProjectItemRef m_operationItem;
    workshop::ProjectItemRef m_gcodeItem;
    ToolpathRevision m_revision;
    std::string m_contentFingerprint;
};

// The setup identity is the only bridge from Prepare Carve into Run. It is a
// value snapshot: RunCoordinator receives no preparation flow or repository.
class RunSetupIdentity final {
  public:
    RunSetupIdentity(carve_preparation::PrepareCarvePin preparation,
                     ToolpathIdentity toolpath);

    [[nodiscard]] const carve_preparation::PrepareCarvePin& preparation() const noexcept;
    [[nodiscard]] const ToolpathIdentity& toolpath() const noexcept;
    [[nodiscard]] bool valid() const noexcept;

    friend bool operator==(const RunSetupIdentity& lhs,
                           const RunSetupIdentity& rhs) noexcept;

  private:
    carve_preparation::PrepareCarvePin m_preparation;
    ToolpathIdentity m_toolpath;
};

enum class RunPreflightCheck : std::size_t {
    MachineConnected,
    ControllerIdle,
    Homed,
    WorkZeroSet,
    ToolConfirmed,
    StockSecured,
    OutlineConfirmed,
};

[[nodiscard]] const std::array<RunPreflightCheck, 7>&
requiredRunPreflightChecks() noexcept;

// Facts are immutable after construction. They are deliberately plain safety
// evidence, not callbacks into a controller or mutable machine state.
class RunPreflightFacts final {
  public:
    explicit RunPreflightFacts(std::array<bool, 7> satisfied) noexcept;

    [[nodiscard]] static RunPreflightFacts allSatisfied() noexcept;
    [[nodiscard]] bool satisfied(RunPreflightCheck check) const noexcept;
    [[nodiscard]] bool passed() const noexcept;
    [[nodiscard]] std::vector<RunPreflightCheck> missingChecks() const;

    friend bool operator==(const RunPreflightFacts& lhs,
                           const RunPreflightFacts& rhs) noexcept;

  private:
    std::array<bool, 7> m_satisfied;
};

// Preflight is bound to the exact setup it checked. A RunPackage cannot become
// valid merely because some other setup passed preflight moments later.
class RunPreflightSnapshot final {
  public:
    RunPreflightSnapshot(RunSetupIdentity setup,
                         PreflightRevision revision,
                         RunPreflightFacts facts);

    [[nodiscard]] const RunSetupIdentity& setup() const noexcept;
    [[nodiscard]] PreflightRevision revision() const noexcept;
    [[nodiscard]] const RunPreflightFacts& facts() const noexcept;
    [[nodiscard]] bool passed() const noexcept;

    friend bool operator==(const RunPreflightSnapshot& lhs,
                           const RunPreflightSnapshot& rhs) noexcept;

  private:
    RunSetupIdentity m_setup;
    PreflightRevision m_revision;
    RunPreflightFacts m_facts;
};

class RunIdentity final {
  public:
    RunIdentity(workshop::RunId run, RunSetupIdentity setup);

    [[nodiscard]] workshop::RunId run() const noexcept;
    [[nodiscard]] const RunSetupIdentity& setup() const noexcept;
    [[nodiscard]] bool valid() const noexcept;

    friend bool operator==(const RunIdentity& lhs,
                           const RunIdentity& rhs) noexcept;

  private:
    workshop::RunId m_run;
    RunSetupIdentity m_setup;
};

enum class RunPackageIssue {
    None,
    InvalidRunId,
    InvalidPreparationIdentity,
    InvalidToolpathIdentity,
    SetupIdentityMismatch,
    InvalidPreflightRevision,
    PreflightBindingMismatch,
    PreflightFailed,
};

// No mutator is exposed. RunCoordinator stores this exact value and never
// refreshes it from the currently selected project, operation, or G-code.
class RunPackage final {
  public:
    RunPackage(RunIdentity identity, RunPreflightSnapshot preflight);

    [[nodiscard]] const RunIdentity& identity() const noexcept;
    [[nodiscard]] const RunPreflightSnapshot& preflight() const noexcept;
    [[nodiscard]] RunPackageIssue issue() const noexcept;
    [[nodiscard]] bool valid() const noexcept;

    friend bool operator==(const RunPackage& lhs,
                           const RunPackage& rhs) noexcept;

  private:
    RunIdentity m_identity;
    RunPreflightSnapshot m_preflight;
};

} // namespace dw::run_coordination

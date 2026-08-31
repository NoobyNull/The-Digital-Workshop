#pragma once

namespace dw {

enum class CncStreamOrigin {
    ExternalGCode,
    DirectCarve,
};

enum class CncStreamShell {
    Sender,
    GuidedWorkshop,
};

[[nodiscard]] constexpr CncStreamShell cncStreamShellForStart(
    CncStreamOrigin origin,
    bool guidedExperience,
    bool workshopActive) noexcept {
    return origin == CncStreamOrigin::DirectCarve && guidedExperience && workshopActive
               ? CncStreamShell::GuidedWorkshop
               : CncStreamShell::Sender;
}

} // namespace dw

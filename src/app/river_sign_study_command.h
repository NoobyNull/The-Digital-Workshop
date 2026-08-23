#pragma once

#include <string>
#include <vector>

#include "core/types.h"

namespace dw::river_sign_study {

struct Command {
    bool requested = false;
    Path fixtureDirectory;
    std::string error;

    [[nodiscard]] bool valid() const noexcept { return error.empty(); }
};

// Parses only the explicit study request and its incompatible modes. All
// unrelated application arguments remain owned by the normal CLI parser.
[[nodiscard]] Command parseCommand(const std::vector<std::string>& arguments);

} // namespace dw::river_sign_study

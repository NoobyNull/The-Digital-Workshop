#include "app/river_sign_study_command.h"

namespace dw::river_sign_study {

Command parseCommand(const std::vector<std::string>& arguments) {
    Command result;
    bool incompatibleMode = false;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        if (argument == "--diagnostic" || argument == "-d" ||
            argument == "--ux-capture") {
            incompatibleMode = true;
            continue;
        }
        if (argument != "--river-sign-study")
            continue;
        if (result.requested) {
            result.error = "--river-sign-study may be specified only once";
            return result;
        }
        result.requested = true;
        if (index + 1 >= arguments.size() || arguments[index + 1].empty() ||
            arguments[index + 1].front() == '-') {
            result.error = "--river-sign-study requires a fixture directory";
            return result;
        }
        result.fixtureDirectory = arguments[++index];
    }

    if (result.requested && incompatibleMode) {
        result.error =
            "--river-sign-study cannot be combined with diagnostic or UX capture mode";
    }
    return result;
}

} // namespace dw::river_sign_study

#include "file_filter_utils.h"

#include <cctype>

namespace dw::file_dialog {
namespace {

std::string trim(std::string_view value) {
    size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

} // namespace

std::string toNativeFilterSpec(std::string_view extensions) {
    std::string spec;
    size_t pos = 0;

    while (pos <= extensions.size()) {
        const size_t next = extensions.find_first_of(";,", pos);
        const size_t count =
            next == std::string_view::npos ? std::string_view::npos : next - pos;
        std::string token = trim(extensions.substr(pos, count));

        if (token != "*" && token != "*.*") {
            while (!token.empty() && token.front() == '*') {
                token.erase(token.begin());
            }
            if (!token.empty() && token.front() == '.') {
                token.erase(token.begin());
            }

            if (!token.empty()) {
                if (!spec.empty()) {
                    spec += ',';
                }
                spec += token;
            }
        }

        if (next == std::string_view::npos) {
            break;
        }
        pos = next + 1;
    }

    return spec;
}

} // namespace dw::file_dialog

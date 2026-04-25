#include "direct_carve_zeroing_probe.h"

#include <cstdlib>

namespace dw {
namespace carve {

namespace {

bool parseFloat(const char*& cursor, f32& out)
{
    char* end = nullptr;
    out = std::strtof(cursor, &end);
    if (end == cursor) {
        return false;
    }
    cursor = end;
    return true;
}

bool consume(const char*& cursor, char expected)
{
    if (*cursor != expected) {
        return false;
    }
    ++cursor;
    return true;
}

} // namespace

SienciAutoZeroProfile defaultSienciAutoZeroProfile()
{
    return SienciAutoZeroProfile{};
}

std::optional<DirectCarveProbeResult>
parseGrblProbeResult(const std::string& line)
{
    constexpr const char* prefix = "[PRB:";
    if (line.rfind(prefix, 0) != 0 || line.empty() || line.back() != ']') {
        return std::nullopt;
    }

    const char* cursor = line.c_str() + 5;
    DirectCarveProbeResult result;
    if (!parseFloat(cursor, result.position.x) || !consume(cursor, ',') ||
        !parseFloat(cursor, result.position.y) || !consume(cursor, ',') ||
        !parseFloat(cursor, result.position.z) || !consume(cursor, ':')) {
        return std::nullopt;
    }

    if (*cursor != '0' && *cursor != '1') {
        return std::nullopt;
    }
    result.contact = (*cursor == '1');
    ++cursor;

    if (*cursor != ']') {
        return std::nullopt;
    }
    return result;
}

} // namespace carve
} // namespace dw

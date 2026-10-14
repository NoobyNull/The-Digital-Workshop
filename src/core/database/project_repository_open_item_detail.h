#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../types.h"

namespace dw {

class Database;

namespace project_open_item_detail {

[[nodiscard]] std::string jsonEscape(const std::string& value);
using GeneratedDirectCarvePrograms = std::map<i64, i64>;
[[nodiscard]] GeneratedDirectCarvePrograms explicitGeneratedDirectCarvePrograms(
    Database& database,
    i64 projectId);
[[nodiscard]] bool operationGroupNeedsOpenItem(
    Database& database,
    i64 projectId,
    i64 groupId,
    const GeneratedDirectCarvePrograms& explicitPrograms);
[[nodiscard]] std::optional<i64> operationGroupOpenItemParent(Database& database,
                                                              i64 projectId,
                                                              i64 gcodeId);
[[nodiscard]] bool jsonTextSemanticallyEqual(const std::string& left,
                                             const std::string& right);
void pruneOpenItemsForProject(Database& database, i64 projectId);

} // namespace project_open_item_detail
} // namespace dw

#pragma once

#include "../types.h"

namespace dw {

class Mesh;

namespace mesh_repair {

struct RecalculateNormalsResult {
    bool repaired = false;
    u64 previousHash = 0;
    u64 newHash = 0;
};

RecalculateNormalsResult recalculateNormals(Mesh& mesh);

} // namespace mesh_repair
} // namespace dw

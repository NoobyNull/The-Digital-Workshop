#include "mesh_repair.h"

#include "mesh.h"

namespace dw::mesh_repair {

RecalculateNormalsResult recalculateNormals(Mesh& mesh) {
    if (!mesh.isValid()) {
        return {};
    }

    RecalculateNormalsResult result;
    result.previousHash = mesh.geometryHash();
    mesh.recalculateNormals();
    result.newHash = mesh.geometryHash();
    result.repaired = true;
    return result;
}

} // namespace dw::mesh_repair

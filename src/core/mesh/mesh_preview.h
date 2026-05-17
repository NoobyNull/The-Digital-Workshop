#pragma once

#include <cstddef>

#include "mesh.h"

namespace dw::mesh_preview {

Mesh sampleTrianglesForPreview(const Mesh& mesh, std::size_t maxTriangles);

} // namespace dw::mesh_preview

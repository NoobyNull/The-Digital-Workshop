#include "model_fitter.h"

#include <algorithm>
#include <cmath>

namespace dw {
namespace carve {

void ModelFitter::setModelBounds(const Vec3& min, const Vec3& max)
{
    m_modelMin = min;
    m_modelMax = max;
}

void ModelFitter::setStock(const StockDimensions& stock)
{
    m_stock = stock;
}

void ModelFitter::setMachineTravel(f32 travelX, f32 travelY, f32 travelZ)
{
    m_travelX = travelX;
    m_travelY = travelY;
    m_travelZ = travelZ;
}

FitResult ModelFitter::fit(const FitParams& params) const
{
    FitResult result;

    const f32 modelExtX = m_modelMax.x - m_modelMin.x;
    const f32 modelExtY = m_modelMax.y - m_modelMin.y;
    const f32 modelExtZ = m_modelMax.z - m_modelMin.z;

    // Scaled extents (uniform XY scale)
    const f32 extX = modelExtX * params.scale;
    const f32 extY = modelExtY * params.scale;

    // Z depth: use explicit depth if non-zero, else full model Z range scaled
    const f32 depth = (params.depthMm > 0.0f)
        ? params.depthMm
        : modelExtZ * params.scale;

    // Compute transformed bounds on stock surface
    // Origin is at stock corner; Z=0 at bottom, Z=thickness at top
    result.modelMin = Vec3{
        params.offsetX,
        params.offsetY,
        m_stock.thickness - depth
    };
    result.modelMax = Vec3{
        params.offsetX + extX,
        params.offsetY + extY,
        m_stock.thickness
    };

    // Check stock fit
    result.fitsStock = (extX <= m_stock.width) &&
                       (extY <= m_stock.height) &&
                       (depth <= m_stock.thickness);

    // Check machine travel fit
    result.fitsMachine = true;
    if (m_travelX > 0.0f && result.modelMax.x > m_travelX) {
        result.fitsMachine = false;
    }
    if (m_travelY > 0.0f && result.modelMax.y > m_travelY) {
        result.fitsMachine = false;
    }
    if (m_travelZ > 0.0f && m_stock.thickness > m_travelZ) {
        result.fitsMachine = false;
    }

    // Build warning message
    if (!result.fitsStock || !result.fitsMachine) {
        std::string warn;
        if (extX > m_stock.width) {
            warn += "Model width (" + std::to_string(extX) +
                    " mm) exceeds material blank width (" +
                    std::to_string(m_stock.width) + " mm). ";
        }
        if (extY > m_stock.height) {
            warn += "Model height (" + std::to_string(extY) +
                    " mm) exceeds material blank height (" +
                    std::to_string(m_stock.height) + " mm). ";
        }
        if (depth > m_stock.thickness) {
            warn += "Carve depth (" + std::to_string(depth) +
                    " mm) exceeds material blank thickness (" +
                    std::to_string(m_stock.thickness) + " mm). ";
        }
        if (!result.fitsMachine && result.fitsStock) {
            warn += "Model exceeds machine travel limits. ";
        }
        result.warning = warn;
    }

    return result;
}

f32 ModelFitter::autoScale() const
{
    const f32 modelExtX = m_modelMax.x - m_modelMin.x;
    const f32 modelExtY = m_modelMax.y - m_modelMin.y;

    const f32 modelExtZ = m_modelMax.z - m_modelMin.z;

    if (modelExtX <= 0.0f || modelExtY <= 0.0f ||
        m_stock.width <= 0.0f || m_stock.height <= 0.0f) {
        return 1.0f;
    }

    // Fit to stock (XY + Z thickness) while maintaining aspect ratio, capped at 1.0
    f32 scaleXY = std::min(m_stock.width / modelExtX,
                           m_stock.height / modelExtY);
    f32 scaleZ = (modelExtZ > 0.0f && m_stock.thickness > 0.0f)
                     ? m_stock.thickness / modelExtZ
                     : scaleXY;
    return std::min({1.0f, scaleXY, scaleZ});
}

f32 ModelFitter::autoDepth() const
{
    return m_modelMax.z - m_modelMin.z;
}

Vec3 ModelFitter::transform(const Vec3& modelPoint, const FitParams& params) const
{
    const f32 modelExtZ = m_modelMax.z - m_modelMin.z;
    const f32 depth = (params.depthMm > 0.0f)
        ? params.depthMm
        : modelExtZ * params.scale;

    // Normalize model point to [0, 1] within model bounds
    const f32 nx = (modelPoint.x - m_modelMin.x) / (m_modelMax.x - m_modelMin.x);
    const f32 ny = (modelPoint.y - m_modelMin.y) / (m_modelMax.y - m_modelMin.y);
    const f32 nz = (modelPoint.z - m_modelMin.z) / (m_modelMax.z - m_modelMin.z);

    // Scale and position on stock
    const f32 extX = (m_modelMax.x - m_modelMin.x) * params.scale;
    const f32 extY = (m_modelMax.y - m_modelMin.y) * params.scale;

    return Vec3{
        params.offsetX + nx * extX,
        params.offsetY + ny * extY,
        m_stock.thickness - depth + nz * depth
    };
}

} // namespace carve
} // namespace dw

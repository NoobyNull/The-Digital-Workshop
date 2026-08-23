// Saved Direct Carve operation restoration and parent-model handoff.

#include "ui/panels/direct_carve_panel.h"

#include <algorithm>
#include <utility>

#include "core/carve/material_blank_defaults.h"
#include "core/materials/material_manager.h"

namespace dw {
namespace {

bool pinMatchesOperation(const carve_preparation::PrepareCarvePin& pin,
                         const ProjectOpenItem& item) {
    const auto project = pin.project();
    const auto model = pin.modelItem();
    const auto operation = pin.operationItem();
    const auto source = pin.modelSource();
    return project.valid() && model.valid() && operation.valid() && source.valid() &&
           source.kind == workshop::LibraryItemKind::Model && pin.token().valid() &&
           model.project == project && operation.project == project &&
           operation.item.value == item.id && item.projectId == project.value &&
           item.itemType == ProjectOpenItemType::Operation && item.parentItemId.has_value() &&
           *item.parentItemId == model.item.value;
}

} // namespace

void DirectCarvePanel::onModelLoaded(const std::vector<Vertex>& vertices,
                                     const std::vector<u32>& indices,
                                     const Vec3& boundsMin,
                                     const Vec3& boundsMax,
                                     const std::string& modelName,
                                     const Path& modelSourcePath,
                                     u32 thumbnailTexture,
                                     bool notifyFitPreview) {
    m_modelVertices = vertices;
    m_modelIndices = indices;
    m_modelLoaded = true;
    m_modelBoundsMin = boundsMin;
    m_modelBoundsMax = boundsMax;
    m_fitter.setModelBounds(boundsMin, boundsMax);
    if (!modelName.empty())
        m_modelName = modelName;
    if (!modelSourcePath.empty())
        m_modelSourcePath = modelSourcePath;
    m_modelThumbnail = thumbnailTexture;

    // Initialize the material blank from the loaded model; machine travel is checked separately.
    if (m_stock.width <= 0.0f || m_stock.height <= 0.0f) {
        m_stock = carve::materialBlankFromModelBounds(boundsMin, boundsMax);
    }

    m_fitter.setStock(m_stock);
    m_fitParams.scale = m_fitter.autoScale();
    bool restoredOperation = false;
    if (m_pendingOperationSetup) {
        m_restoringOperationSetup = true;
        applyOperationSetup(*m_pendingOperationSetup);
        m_restoringOperationSetup = false;
        m_pendingOperationSetup.reset();
        restoredOperation = true;
    }

    if (!m_modelName.empty()) {
        m_title = m_modelName + "###Direct Carve";
    }

    if (notifyFitPreview && m_onFitParamsChanged && m_stock.width > 0.0f && m_stock.height > 0.0f) {
        m_onFitParamsChanged(m_fitParams, m_modelBoundsMin, m_modelBoundsMax, m_stock);
    }

    // applyOperationSetup() invalidates generated geometry before restoring its
    // saved clearing tool. Do not invalidate it a second time here.
    if (!restoredOperation) {
        markGeometryChanged();
    }
    m_maxStepVisited = std::max(m_maxStepVisited, static_cast<int>(m_currentStep));
}

bool DirectCarvePanel::loadOperationOpenItem(const ProjectOpenItem& item,
                                             carve_preparation::PrepareCarvePin pin) {
    auto setup = carve::parseDirectCarveOperationSetup(item);
    if (!setup || !pinMatchesOperation(pin, item)) {
        return false;
    }

    // Preserve only the freshly loaded parent geometry across the reset. Every
    // prior operation choice and workflow gate belongs to the old context.
    const bool modelLoaded = m_modelLoaded;
    auto modelVertices = std::move(m_modelVertices);
    auto modelIndices = std::move(m_modelIndices);
    const Vec3 modelBoundsMin = m_modelBoundsMin;
    const Vec3 modelBoundsMax = m_modelBoundsMax;
    auto modelName = std::move(m_modelName);
    auto modelSourcePath = std::move(m_modelSourcePath);
    const u32 modelThumbnail = m_modelThumbnail;

    clearProjectContext();
    m_preparationPin = std::move(pin);
    m_restoringOperationSetup = true;
    if (modelLoaded) {
        m_modelLoaded = true;
        m_modelVertices = std::move(modelVertices);
        m_modelIndices = std::move(modelIndices);
        m_modelBoundsMin = modelBoundsMin;
        m_modelBoundsMax = modelBoundsMax;
        m_modelName = std::move(modelName);
        m_modelSourcePath = std::move(modelSourcePath);
        m_modelThumbnail = modelThumbnail;
        m_fitter.setModelBounds(modelBoundsMin, modelBoundsMax);
        applyOperationSetup(*setup);
    } else {
        // Support callers that stage the operation before their model loader
        // completes. onModelLoaded() consumes this setup exactly once.
        m_pendingOperationSetup = *setup;
        m_modelName = setup->modelName;
        m_modelSourcePath = setup->modelSourcePath;
        m_stock = setup->stock;
        m_fitParams = setup->fit;
        m_toolpathConfig = setup->toolpath;
        if (!setup->modelName.empty()) {
            m_title = setup->modelName + "###Direct Carve";
        }
    }
    m_restoringOperationSetup = false;
    beginPinnedPreparation();
    m_open = true;
    return true;
}

void DirectCarvePanel::applyOperationSetup(const carve::DirectCarveOperationSetup& setup) {
    // A loaded parent model is the authoritative identity. Snapshot paths and
    // names may be stale after a library move or rename.
    if (!m_modelLoaded) {
        if (!setup.modelName.empty()) {
            m_modelName = setup.modelName;
            m_title = setup.modelName + "###Direct Carve";
        }
        if (!setup.modelSourcePath.empty()) {
            m_modelSourcePath = setup.modelSourcePath;
        }
    }

    m_stock = setup.stock;
    m_fitParams = setup.fit;
    m_toolpathConfig = setup.toolpath;
    m_fitter.setStock(m_stock);

    if (setup.materialId.has_value() || !setup.materialName.empty()) {
        if (!m_materialListLoaded && m_materialMgr) {
            m_materialListLoaded = true;
            m_materialList = m_materialMgr->getAllMaterials();
        }

        m_selectedMaterialIdx = -1;
        for (int i = 0; i < static_cast<int>(m_materialList.size()); ++i) {
            const auto& material = m_materialList[static_cast<size_t>(i)];
            if ((setup.materialId.has_value() && material.id == *setup.materialId) ||
                (!setup.materialName.empty() && material.name == setup.materialName)) {
                m_selectedMaterialIdx = i;
                m_materialName = material.name;
                break;
            }
        }
        if (m_selectedMaterialIdx < 0) {
            m_materialName = setup.materialName;
        }
        m_materialSelected = true;
    }

    m_toolpathGenerated = false;
    markGeometryChanged();

    m_toolPlan = carve::DirectCarveToolPlan{};
    m_toolSelectionMessage.clear();
    if (setup.finishingTool) {
        (void)m_toolPlan.selectTool(carve::DirectCarveToolPickerRole::Finishing,
                                    *setup.finishingTool);
    }
    if (setup.selectedClearingTool) {
        const auto restored = m_toolPlan.selectTool(carve::DirectCarveToolPickerRole::Clearing,
                                                    *setup.selectedClearingTool);
        if (!restored)
            m_toolSelectionMessage = restored.message;
    }
    m_toolPlan.setClearingMode(setup.clearingToolMode);
    if (setup.effectiveClearingTool &&
        setup.clearingToolMode != carve::ClearingToolMode::Disabled) {
        (void)m_toolPlan.confirmEffectiveClearing(setup.effectiveClearingTool, true);
    }
    m_toolSetupConfirmed = false;
    m_autoRoughingWarning.clear();

    if (!m_materialSelected) {
        m_currentStep = Step::MaterialSetup;
    } else if (!m_toolPlan.finishingIntent()) {
        m_currentStep = Step::ToolSelect;
    } else {
        m_currentStep = Step::Preview;
    }
    m_maxStepVisited = std::max(m_maxStepVisited, static_cast<int>(m_currentStep));

    if (m_onFitParamsChanged && m_modelLoaded) {
        m_onFitParamsChanged(m_fitParams, m_modelBoundsMin, m_modelBoundsMax, m_stock);
    }
}

} // namespace dw

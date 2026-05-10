// Application callbacks — model selection, material assignment, texture loading.

#include "app/application.h"

#include <chrono>
#include <future>

#include "app/workspace.h"
#include "core/config/config.h"
#include "core/database/connection_pool.h"
#include "core/database/model_repository.h"
#include "core/import/smart_tagger.h"
#include "core/library/library_manager.h"
#include "core/loaders/loader_factory.h"
#include "core/loaders/texture_loader.h"
#include "core/materials/material_archive.h"
#include "core/materials/material_manager.h"
#include "core/paths/path_resolver.h"
#include "core/threading/main_thread_queue.h"
#include "core/utils/log.h"
#include <glad/gl.h>

#include "managers/ui_manager.h"
#include "render/texture.h"
#include "ui/panels/direct_carve_panel.h"
#include "ui/panels/library_panel.h"
#include "ui/panels/materials_panel.h"
#include "ui/panels/properties_panel.h"
#include "ui/panels/viewport_panel.h"
#include "ui/widgets/toast.h"

namespace dw {

void Application::onModelSelected(int64_t modelId) {
    if (!m_libraryManager) return;

    if (m_focusedModelId > 0 && m_uiManager->viewportPanel()) {
        auto camState = m_uiManager->viewportPanel()->getCameraState();
        ModelRepository repo(*m_database);
        repo.updateCameraState(m_focusedModelId, camState);
    }

    auto record = m_libraryManager->getModel(modelId);
    if (!record) return;
    m_focusedModelId = modelId;

    if (m_materialManager) {
        auto assignedMaterial = m_materialManager->getModelMaterial(modelId);
        if (assignedMaterial) {
            loadMaterialTextureForModel(modelId);
            if (m_uiManager->propertiesPanel())
                m_uiManager->propertiesPanel()->setMaterial(*assignedMaterial);
        } else {
            i64 defaultId = Config::instance().getDefaultMaterialId();
            if (defaultId > 0 && m_materialManager->getMaterial(defaultId)) {
                assignMaterialToCurrentModel(defaultId);
            } else {
                auto allMaterials = m_materialManager->getAllMaterials();
                if (!allMaterials.empty()) {
                    assignMaterialToCurrentModel(allMaterials.front().id);
                } else {
                    m_activeMaterialTexture.reset();
                    m_activeMaterialId = -1;
                    if (m_uiManager->propertiesPanel())
                        m_uiManager->propertiesPanel()->clearMaterial();
                }
            }
        }
    }

    uint64_t gen = ++m_loadingState.generation;
    m_loadingState.set(record->name);
    if (m_loadThread.joinable()) m_loadThread.join();

    Path filePath = PathResolver::resolve(record->filePath, PathCategory::Support);
    std::string name = record->name;
    auto storedOrientYaw = record->orientYaw;
    auto storedOrientMatrix = record->orientMatrix;
    auto storedCamera = record->cameraState;

    m_loadThread = std::thread(
        [this, filePath, name, gen, modelId, storedOrientYaw, storedOrientMatrix, storedCamera]() {
            auto loadResult = LoaderFactory::load(filePath);
            if (!loadResult) {
                log::errorf("Application",
                            "Failed to open library model '%s' from '%s': %s",
                            name.c_str(),
                            filePath.string().c_str(),
                            loadResult.error.c_str());
                m_mainThreadQueue->enqueue([this, name, error = loadResult.error, gen]() {
                    if (gen == m_loadingState.generation.load()) {
                        m_loadingState.reset();
                    }
                    ToastManager::instance().show(
                        ToastType::Error,
                        "Model Open Failed",
                        name + ": " + (error.empty() ? "failed to load file" : error));
                });
                return;
            }
            loadResult.mesh->setName(name);
            f32 orientYaw = 0.0f;
            if (Config::instance().getAutoOrient()) {
                if (storedOrientYaw && storedOrientMatrix) {
                    loadResult.mesh->applyStoredOrient(*storedOrientMatrix);
                    orientYaw = *storedOrientYaw;
                } else {
                    orientYaw = loadResult.mesh->autoOrient();
                    ScopedConnection conn(*m_connectionPool);
                    ModelRepository repo(*conn);
                    repo.updateOrient(modelId, orientYaw, loadResult.mesh->getOrientMatrix());
                }
            }
            auto mesh = loadResult.mesh;
            m_mainThreadQueue->enqueue([this, mesh, name, filePath, gen, orientYaw, storedCamera]() {
                if (gen != m_loadingState.generation.load()) return;
                m_loadingState.reset();
                m_workspace->setFocusedMesh(mesh);
                if (m_uiManager->viewportPanel())
                    m_uiManager->viewportPanel()->setPreOrientedMesh(mesh, orientYaw, storedCamera);
                if (m_uiManager->propertiesPanel())
                    m_uiManager->propertiesPanel()->setMesh(mesh, name);
                if (m_uiManager->materialsPanel())
                    m_uiManager->materialsPanel()->setModelLoaded(true);
                if (m_uiManager->directCarvePanel() && mesh) {
                    GLuint thumb = 0;
                    if (m_uiManager->libraryPanel())
                        thumb = m_uiManager->libraryPanel()->getThumbnailTextureForModel(
                            m_focusedModelId);
                    m_uiManager->directCarvePanel()->onModelLoaded(
                        mesh->vertices(), mesh->indices(),
                        mesh->bounds().min, mesh->bounds().max,
                        name, filePath, thumb);
                }
            });
        });
}

void Application::assignMaterialToCurrentModel(int64_t materialId) {
    if (!m_materialManager || !m_workspace)
        return;
    auto mesh = m_workspace->getFocusedMesh();
    if (!mesh)
        return;
    auto material = m_materialManager->getMaterial(materialId);
    if (!material)
        return;
    if (m_focusedModelId > 0)
        m_materialManager->assignMaterialToModel(materialId, m_focusedModelId);

    // Load and upload material texture if archive path exists
    m_activeMaterialTexture.reset();
    if (!material->archivePath.empty()) {
        Path archPath = PathResolver::resolve(material->archivePath, PathCategory::Materials);
        auto matData = MaterialArchive::load(archPath.string());
        if (matData && !matData->textureData.empty()) {
            auto textureOpt = TextureLoader::loadPNGFromMemory(matData->textureData.data(),
                                                               matData->textureData.size());
            if (textureOpt) {
                m_activeMaterialTexture = std::make_unique<Texture>();
                m_activeMaterialTexture->upload(textureOpt->pixels.data(),
                                                textureOpt->width,
                                                textureOpt->height);
            }
        }
    }

    if (mesh->needsUVGeneration())
        mesh->generatePlanarUVs(material->grainDirectionDeg);

    m_activeMaterialId = materialId;

    if (m_uiManager->propertiesPanel()) {
        m_uiManager->propertiesPanel()->setMaterial(*material);
    }

    if (m_uiManager->viewportPanel()) {
        m_uiManager->viewportPanel()->setMaterialTexture(m_activeMaterialTexture.get());
        m_uiManager->viewportPanel()->setMesh(mesh);
    }
}

void Application::loadMaterialTextureForModel(int64_t modelId) {
    if (!m_materialManager)
        return;

    auto material = m_materialManager->getModelMaterial(modelId);
    if (!material) {
        m_activeMaterialTexture.reset();
        m_activeMaterialId = -1;
        if (m_uiManager && m_uiManager->viewportPanel())
            m_uiManager->viewportPanel()->setMaterialTexture(nullptr);
        return;
    }

    m_activeMaterialId = material->id;
    m_activeMaterialTexture.reset();

    if (!material->archivePath.empty()) {
        Path archPath = PathResolver::resolve(material->archivePath, PathCategory::Materials);
        auto matData = MaterialArchive::load(archPath.string());
        if (matData && !matData->textureData.empty()) {
            auto textureOpt = TextureLoader::loadPNGFromMemory(matData->textureData.data(),
                                                               matData->textureData.size());
            if (textureOpt) {
                m_activeMaterialTexture = std::make_unique<Texture>();
                m_activeMaterialTexture->upload(textureOpt->pixels.data(),
                                                textureOpt->width,
                                                textureOpt->height);
            }
        }
    }

    if (m_uiManager && m_uiManager->viewportPanel())
        m_uiManager->viewportPanel()->setMaterialTexture(m_activeMaterialTexture.get());
}

bool Application::generateMaterialThumbnail(int64_t modelId, Mesh& mesh) {
    return generateMaterialThumbnail(modelId, mesh, ThumbnailView::Unknown);
}

bool Application::generateMaterialThumbnail(int64_t modelId, Mesh& mesh, ThumbnailView view) {
    float cameraYaw = 0.0f;
    if (Config::instance().getAutoOrient()) {
        auto record = m_libraryManager->getModel(modelId);
        if (record && record->orientYaw)
            cameraYaw = *record->orientYaw;

        if (!mesh.wasAutoOriented()) {
            if (record && record->orientMatrix)
                mesh.applyStoredOrient(*record->orientMatrix);
            else
                cameraYaw = mesh.autoOrient();
        }
    }

    float cameraPitch = 0.0f;
    if (view != ThumbnailView::Unknown) {
        auto camera = smart_tagging::cameraForView(view, cameraYaw);
        cameraYaw = camera.yawDeg;
        cameraPitch = camera.pitchDeg;
    }

    std::unique_ptr<Texture> tex;
    i64 matId = Config::instance().getDefaultMaterialId();
    std::optional<MaterialRecord> mat;
    if (matId > 0 && m_materialManager)
        mat = m_materialManager->getMaterial(matId);
    if (!mat && m_materialManager) {
        auto all = m_materialManager->getAllMaterials();
        if (!all.empty()) mat = all.front();
    }
    if (mat) {
        if (!mat->archivePath.empty()) {
            auto matData = MaterialArchive::load(mat->archivePath.string());
            if (matData && !matData->textureData.empty()) {
                auto decoded = TextureLoader::loadPNGFromMemory(matData->textureData.data(),
                                                                matData->textureData.size());
                if (decoded) {
                    tex = std::make_unique<Texture>();
                    tex->upload(decoded->pixels.data(), decoded->width, decoded->height);
                }
            }
        }
        if (mesh.needsUVGeneration())
            mesh.generatePlanarUVs(mat->grainDirectionDeg);
    }

    log::infof("Thumbnail",
               "Generating model=%lld view=%s cameraPitch=%.2f cameraYaw=%.2f",
               static_cast<long long>(modelId),
               smart_tagging::thumbnailViewName(view),
               static_cast<double>(cameraPitch),
               static_cast<double>(cameraYaw));
    bool ok = m_libraryManager->generateThumbnail(modelId, mesh, tex.get(), cameraPitch, cameraYaw);
    log::infof("Thumbnail",
               "Generated model=%lld view=%s result=%s",
               static_cast<long long>(modelId),
               smart_tagging::thumbnailViewName(view),
               ok ? "ok" : "failed");
    return ok;
}

bool Application::regenerateSmartTagThumbnail(int64_t modelId, ThumbnailView view) {
    if (!m_mainThreadQueue || !m_libraryManager)
        return false;

    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();
    m_mainThreadQueue->enqueue([this, modelId, view, promise]() {
        auto record = m_libraryManager->getModel(modelId);
        if (!record) {
            promise->set_value(false);
            return;
        }

        auto loadResult =
            LoaderFactory::load(PathResolver::resolve(record->filePath, PathCategory::Support));
        if (!loadResult) {
            promise->set_value(false);
            return;
        }

        bool ok = generateMaterialThumbnail(modelId, *loadResult.mesh, view);
        if (ok && m_uiManager && m_uiManager->libraryPanel()) {
            m_uiManager->libraryPanel()->invalidateThumbnail(modelId);
            m_uiManager->libraryPanel()->refresh();
        }
        promise->set_value(ok);
    });

    if (future.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
        log::warningf("Thumbnail",
                      "Timed out waiting for smart-tag thumbnail model=%lld view=%s",
                      static_cast<long long>(modelId),
                      smart_tagging::thumbnailViewName(view));
        return false;
    }
    return future.get();
}

bool Application::applyAiOrientationCorrection(int64_t modelId, int clockwiseDegrees) {
    if (!m_connectionPool)
        return false;

    ScopedConnection conn(*m_connectionPool);
    ModelRepository repo(*conn);
    auto record = repo.findById(modelId);
    if (!record)
        return false;

    auto loadResult =
        LoaderFactory::load(PathResolver::resolve(record->filePath, PathCategory::Support));
    if (!loadResult) {
        log::warningf("Orientation",
                      "Failed to load model %lld for AI orientation correction: %s",
                      static_cast<long long>(modelId),
                      loadResult.error.c_str());
        return false;
    }

    f32 orientYaw = record->orientYaw.value_or(0.0f);
    Mat4 baseMatrix(1.0f);
    if (record->orientMatrix) {
        baseMatrix = *record->orientMatrix;
    } else {
        orientYaw = loadResult.mesh->autoOrient();
        baseMatrix = loadResult.mesh->getOrientMatrix();
    }

    Mat4 corrected =
        smart_tagging::orientationCorrectionMatrix(clockwiseDegrees) * baseMatrix;
    bool ok = repo.updateOrient(modelId, orientYaw, corrected);
    log::infof("Orientation",
               "AI orientation correction model=%lld clockwise=%d result=%s",
               static_cast<long long>(modelId),
               clockwiseDegrees,
               ok ? "ok" : "failed");
    return ok;
}

} // namespace dw

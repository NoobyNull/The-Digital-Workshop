#pragma once

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include <imgui.h>

#include "../../core/carve/alignment_validator.h"
#include "../../core/carve/model_fitter.h"
#include "../../core/database/model_repository.h"
#include "../../core/gcode/gcode_types.h"
#include "../../modules/viewport/viewport_presentation.h"
#include "../../render/camera.h"
#include "../../render/framebuffer.h"
#include "../../render/renderer.h"
#include "../../core/cnc/cnc_types.h"
#include "../../render/texture.h"
#include "panel.h"

namespace dw {

// Forward declarations
class Mesh;
using MeshPtr = std::shared_ptr<Mesh>;
class ContextMenuManager;
enum class ViewCubeFace;

enum class ViewportGCodeSource {
    None,
    FileBackedProgram,
    DirectCarvePreview,
};

// 3D viewport panel
class ViewportPanel : public Panel {
  public:
    ViewportPanel();
    ~ViewportPanel() override = default;

    void render() override;

    // Set mesh to display
    void setMesh(MeshPtr mesh);
    // Set mesh that was already auto-oriented on worker thread
    void setPreOrientedMesh(MeshPtr mesh,
                            f32 orientYaw,
                            std::optional<CameraState> savedCamera = std::nullopt);
    void clearMesh();

    // Set toolpath mesh to display
    void setToolpathMesh(MeshPtr toolpathMesh);
    void clearToolpathMesh();

    // Set active material texture for rendering (nullptr = solid color)
    void setMaterialTexture(const Texture* texture) { m_materialTexture = texture; }

    // Camera access
    Camera& camera() { return m_camera; }
    const Camera& camera() const { return m_camera; }

    // Camera state persistence
    CameraState getCameraState() const;
    void restoreCameraState(const CameraState& state);

    // Render settings
    RenderSettings& renderSettings() { return m_renderer.settings(); }

    // Actions
    void resetView();
    void fitToModel();
    bool recalculateModelNormals();
    bool hasValidModel() const;

    // CNC status updates
    void onCncStatusUpdate(const MachineStatus& status) { m_machineStatus = status; }
    void setCncConnected(bool connected) { m_cncConnected = connected; }
    void setSenderWorkspaceActive(bool active) { m_senderWorkspaceActive = active; }

    // G-code line rendering
    void setGCodeProgram(
        gcode::Program program,
        ViewportGCodeSource source = ViewportGCodeSource::FileBackedProgram);
    void clearGCodeProgram();
    bool clearGCodeProgramIfSource(ViewportGCodeSource source);
    bool hasGCode() const { return !m_gcodeProgram.path.empty(); }
    [[nodiscard]] ViewportGCodeSource gcodeSource() const noexcept {
        return m_gcodeSource;
    }

    // Simulation playback
    void updateSimulation(float dt);
    void setGCodeStatistics(const gcode::Statistics& stats);

    // FitParams-based model matrix for alignment overlay
    void setFitParams(const carve::FitParams& params,
                      const Vec3& modelBoundsMin,
                      const Vec3& modelBoundsMax,
                      const carve::StockDimensions& stock);
    void clearFitParams();

    // Context menu manager integration
    void setContextMenuManager(ContextMenuManager* manager) { m_contextMenuManager = manager; }

    // Presentation identity is supplied by the application boundary. The
    // viewport renders the label but owns no Project or Library routing policy.
    void setPresentationIdentity(viewport::PresentationIdentity identity);
    [[nodiscard]] const viewport::PresentationIdentity& presentationIdentity() const {
        return m_presentationIdentity;
    }

  private:
    void registerContextMenuEntries();
    void handleInput();
    void renderViewport();
    void renderToolbar();
    void renderViewCube();
    void renderCncDro();
    void renderIdentityOverlay();
    void snapCameraToView(ViewCubeFace face);
    void rotateViewByQuarterTurns(int quarterTurns);
    void tiltViewByQuarterTurns(int quarterTurns);
    std::optional<Vec3> visibleModelCenter() const;
    void centerCameraOnVisibleModel();

    // ViewCube geometry cache — invalidated when camera orientation changes
    struct ViewCubeCache {
        f32 lastYaw = -999.0f;
        f32 lastPitch = -999.0f;
        std::array<ImVec2, 8> projectedVerts{};
        std::array<f32, 8> depths{};
        struct FaceSort {
            int index;
            f32 avgZ;
        };
        std::array<FaceSort, 6> sortedFaces{};
        bool valid = false;
    };

    Renderer m_renderer;
    Camera m_camera;
    Framebuffer m_framebuffer;

    MeshPtr m_mesh;
    GPUMesh m_gpuMesh;

    MeshPtr m_toolpathMesh;
    GPUMesh m_gpuToolpath;

    // Active material texture (not owned — pointer to Application-managed texture)
    const Texture* m_materialTexture = nullptr;

    // Light drag state
    bool m_lightDirDragging = false;
    bool m_lightIntensityDragging = false;

    // Viewport size
    int m_viewportWidth = 1;
    int m_viewportHeight = 1;

    // ViewCube cache
    ViewCubeCache m_viewCubeCache;

    // CNC live state
    MachineStatus m_machineStatus;
    bool m_cncConnected = false;
    bool m_senderWorkspaceActive = false;

    // Context menu manager (not owned — managed by UIManager)
    ContextMenuManager* m_contextMenuManager = nullptr;
    viewport::PresentationIdentity m_presentationIdentity =
        viewport::PresentationIdentity::none();

    // FitParams model matrix (identity when no alignment active)
    Mat4 m_modelMatrix{1.0f};
    bool m_hasFitParams = false;

    // Alignment validation state
    enum class AlignmentStatus { Unknown, Aligned, Misaligned };
    AlignmentStatus m_alignmentStatus = AlignmentStatus::Unknown;
    bool m_alignmentDirty = false;
    carve::FitParams m_fitParams;
    Vec3 m_fitBoundsMin{0.0f};
    Vec3 m_fitBoundsMax{0.0f};
    carve::StockDimensions m_fitStock;

    // --- Visibility toggles ---
    bool m_showModel = true;
    bool m_showToolpath = true;
    bool m_showGCode = true;
    bool m_showRapids = false;
    bool m_showCuts = true;
    bool m_showPlunges = true;
    bool m_showRetracts = true;
    bool m_colorByTool = false;

    // Z-clip state
    float m_zClipMax = 100.0f;
    float m_zClipMaxBound = 100.0f;

    // --- G-code line rendering ---
    void buildGCodeGeometry();
    void destroyGCodeGeometry();
    void renderGCodeLines();

    gcode::Program m_gcodeProgram;
    ViewportGCodeSource m_gcodeSource = ViewportGCodeSource::None;
    GLuint m_gcodeVAO = 0;
    GLuint m_gcodeVBO = 0;
    u32 m_gcRapidStart = 0, m_gcRapidCount = 0;
    u32 m_gcCutStart = 0, m_gcCutCount = 0;
    u32 m_gcPlungeStart = 0, m_gcPlungeCount = 0;
    u32 m_gcRetractStart = 0, m_gcRetractCount = 0;
    bool m_gcodeDirty = false;

    // Per-tool draw groups (used when m_colorByTool is true)
    struct ToolGroup {
        u32 start = 0;
        u32 count = 0;
        int toolNumber = 0;
    };
    std::vector<ToolGroup> m_gcToolGroups;

    // Tool color palette
    static constexpr int kNumToolColors = 8;
    static Vec3 toolColor(int toolNum);

    // --- Simulation playback state ---
    enum class VPSimState { Stopped, Playing, Paused };

    VPSimState m_simState = VPSimState::Stopped;
    float m_simTime = 0.0f;
    float m_simSpeed = 1.0f;
    float m_simTotalTime = 0.0f;
    size_t m_simSegmentIndex = 0;
    float m_simSegmentProgress = 0.0f;

    std::vector<float> m_segmentTimes;
    std::vector<float> m_segmentTimeCumulative;

    GLuint m_simVAO = 0;
    GLuint m_simVBO = 0;

    void renderSimControls();
    void destroySimGeometry();
};

} // namespace dw

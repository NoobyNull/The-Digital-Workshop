// Digital Workshop - UI Manager (layout & presets)
// Dock layout, config save/restore, layout preset management.

#include "managers/ui_manager.h"

#include <cstring>

#include <imgui.h>
#include <imgui_internal.h>

#include "core/config/config.h"
#include "core/config/layout_migration.h"
#include "modules/workshop/ui/guided_layout_metrics.h"
#include "ui/tool_library_access.h"
#include "ui/panels/viewport_panel.h"

namespace dw {
namespace {

template <typename Predicate>
int findPresetIndex(const std::vector<LayoutPreset>& presets, Predicate predicate) {
    for (int index = 0; index < static_cast<int>(presets.size()); ++index) {
        if (predicate(presets[static_cast<std::size_t>(index)]))
            return index;
    }
    return -1;
}

} // namespace

void UIManager::setupDefaultDockLayout(ImGuiID dockspaceId) {
    const auto* viewport = ImGui::GetMainViewport();
    const auto dockLayout = workshop::ui::chooseGuidedDockLayout(
        viewport->WorkSize.x, ImGui::GetFontSize());

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

    // Split from physical widths so scaled Guided labels remain readable while
    // 4K sidebars stay close to their useful content instead of growing to 20%.
    ImGuiID dockLeft = 0;
    ImGuiID dockCenterRight = 0;
    ImGui::DockBuilderSplitNode(
        dockspaceId,
        ImGuiDir_Left,
        dockLayout.leftSplitRatio,
        &dockLeft,
        &dockCenterRight);

    // Split left sidebar: library (top 60%) | project (bottom 40%)
    ImGuiID dockLeftTop = 0;
    ImGuiID dockLeftBottom = 0;
    ImGui::DockBuilderSplitNode(
        dockLeft, ImGuiDir_Down, 0.40f, &dockLeftBottom, &dockLeftTop);

    // Split center+right using the right sidebar's physical target width.
    ImGuiID dockCenter = 0;
    ImGuiID dockRight = 0;
    ImGui::DockBuilderSplitNode(
        dockCenterRight,
        ImGuiDir_Right,
        dockLayout.rightSplitRatio,
        &dockRight,
        &dockCenter);

    // Core visible panels
    ImGui::DockBuilderDockWindow("Library", dockLeftTop);
    ImGui::DockBuilderDockWindow("Project", dockLeftBottom);
    ImGui::DockBuilderDockWindow("Viewport", dockCenter);
    ImGui::DockBuilderDockWindow("Properties", dockRight);

    // Hidden panels — docked as tabs in existing areas
    ImGui::DockBuilderDockWindow("Home###Start Page", dockCenter);
    ImGui::DockBuilderDockWindow("G-code", dockCenter);
    ImGui::DockBuilderDockWindow("Cut Optimizer", dockCenter);
    ImGui::DockBuilderDockWindow("Project Costing", dockLeftBottom);
    ImGui::DockBuilderDockWindow("Materials", dockLeftBottom);
    ImGui::DockBuilderDockWindow("Tool & Material", dockRight);
    ImGui::DockBuilderDockWindow(kToolLibraryWindowTitle, dockRight);

    // CNC panels — docked as center tabs
    ImGui::DockBuilderDockWindow("CNC Status", dockCenter);
    ImGui::DockBuilderDockWindow("Jog Control", dockCenter);
    ImGui::DockBuilderDockWindow("MDI Console", dockCenter);
    ImGui::DockBuilderDockWindow("WCS", dockCenter);
    ImGui::DockBuilderDockWindow("Safety", dockCenter);
    ImGui::DockBuilderDockWindow("Machine Settings", dockCenter);
    ImGui::DockBuilderDockWindow("Macros", dockCenter);
    ImGui::DockBuilderDockWindow("Job Progress", dockCenter);
    ImGui::DockBuilderDockWindow("CAM", dockCenter);

    ImGui::DockBuilderFinish(dockspaceId);
}

void UIManager::restoreVisibilityFromConfig() {
    auto& cfg = Config::instance();

    m_showViewport = cfg.getShowViewport();
    // Design Library visibility belongs to its task flow, never a saved layout.
    m_showLibrary = false;
    m_showProperties = cfg.getShowProperties();
    m_showProject = cfg.getShowProject();
    m_showMaterials = cfg.getShowMaterials();
    m_showGCode = cfg.getShowGCode();
    m_showCutOptimizer = cfg.getShowCutOptimizer();
    m_showProjectCosting = cfg.getShowProjectCosting();
    m_showToolBrowser = cfg.getShowToolBrowser();
    m_showStartPage = cfg.getShowStartPage();

    m_showCncStatus = cfg.getShowCncStatus();
    m_showCncJog = cfg.getShowCncJog();
    m_showCncConsole = cfg.getShowCncConsole();
    m_showCncWcs = cfg.getShowCncWcs();
    m_showCncTool = cfg.getShowCncTool();
    m_showCncJob = cfg.getShowCncJob();
    m_showCncSafety = cfg.getShowCncSafety();
    m_showCncSettings = cfg.getShowCncSettings();
    m_showCncMacros = cfg.getShowCncMacros();
    m_showDirectCarve = cfg.getShowDirectCarve();

    m_activePresetIndex = cfg.getActiveLayoutPresetIndex();
    if (m_activePresetIndex >= 0 &&
        m_activePresetIndex < static_cast<int>(cfg.getLayoutPresets().size())) {
        const auto& active = cfg.getLayoutPresets()[static_cast<std::size_t>(m_activePresetIndex)];
        if (isGuidedLayout(active) && m_guidedExperienceSetter)
            m_guidedExperienceSetter(true);
        else if (isAdvancedLayout(active) && m_guidedExperienceSetter)
            m_guidedExperienceSetter(false);
    }
    m_workspaceMode = isBuiltInSenderPreset(m_activePresetIndex)
        ? WorkspaceMode::CNC
        : WorkspaceMode::Model;
    syncWorkspaceModeToPanels();
    enforceWorkspaceBoundary();
}

void UIManager::saveVisibilityToConfig() {
    auto& cfg = Config::instance();
    cfg.setShowViewport(m_showViewport);
    // Migrate any legacy persisted Library visibility back to the safe default.
    cfg.setShowLibrary(false);
    cfg.setShowProperties(m_showProperties);
    cfg.setShowProject(m_showProject);
    cfg.setShowMaterials(m_showMaterials);
    cfg.setShowGCode(m_showGCode);
    cfg.setShowCutOptimizer(m_showCutOptimizer);
    cfg.setShowProjectCosting(m_showProjectCosting);
    cfg.setShowToolBrowser(m_showToolBrowser);
    cfg.setShowStartPage(m_showStartPage);

    cfg.setShowCncStatus(m_showCncStatus);
    cfg.setShowCncJog(m_showCncJog);
    cfg.setShowCncConsole(m_showCncConsole);
    cfg.setShowCncWcs(m_showCncWcs);
    cfg.setShowCncTool(m_showCncTool);
    cfg.setShowCncJob(m_showCncJob);
    cfg.setShowCncSafety(m_showCncSafety);
    cfg.setShowCncSettings(m_showCncSettings);
    cfg.setShowCncMacros(m_showCncMacros);
    cfg.setShowDirectCarve(m_showDirectCarve);

    cfg.setActiveLayoutPresetIndex(m_activePresetIndex);
}

void UIManager::applyRenderSettingsFromConfig() {
    if (!m_viewportPanel)
        return;

    auto& cfg = Config::instance();
    auto& rs = m_viewportPanel->renderSettings();
    rs.lightDir = cfg.getRenderLightDir();
    rs.lightColor = cfg.getRenderLightColor();
    rs.ambient = cfg.getRenderAmbient();
    rs.objectColor = cfg.getRenderObjectColor();
    rs.shininess = cfg.getRenderShininess();
    rs.showGrid = cfg.getShowGrid();
    rs.showAxis = cfg.getShowAxis();
}

void UIManager::applyLayoutPreset(int presetIndex) {
    auto& cfg = Config::instance();
    const auto& presets = cfg.getLayoutPresets();
    if (presetIndex < 0 || presetIndex >= static_cast<int>(presets.size()))
        return;
    if (m_cncStreaming && presetIndex != m_activePresetIndex)
        return;
    if (m_showLibrary && isBuiltInSenderPreset(presetIndex))
        return;

    const auto& preset = presets[static_cast<size_t>(presetIndex)];
    for (const auto& entry : m_panelRegistry) {
        const auto* catalog = findWindowCatalogEntry(entry.key);
        if (catalog && !catalog->layoutPersistent)
            continue;
        if (const auto visible = layoutPresetVisibility(preset, entry.key))
            *entry.showFlag = *visible;
    }
    m_activePresetIndex = presetIndex;
    cfg.setActiveLayoutPresetIndex(presetIndex);
    m_suppressAutoContext = true;

    // Keep workspace mode in sync with built-in presets
    if (isBuiltInWorkshopPreset(presetIndex) && !m_cncStreaming)
        m_workspaceMode = WorkspaceMode::Model;
    else if (isBuiltInSenderPreset(presetIndex))
        m_workspaceMode = WorkspaceMode::CNC;

    if (isGuidedLayout(preset) && m_guidedExperienceSetter)
        m_guidedExperienceSetter(true);
    else if (isAdvancedLayout(preset) && m_guidedExperienceSetter)
        m_guidedExperienceSetter(false);

    syncWorkspaceModeToPanels();
    enforceWorkspaceBoundary();
}

bool UIManager::isBuiltInWorkshopPreset(int presetIndex) const {
    const auto& presets = Config::instance().getLayoutPresets();
    if (presetIndex < 0 || presetIndex >= static_cast<int>(presets.size()))
        return false;
    const auto& preset = presets[static_cast<std::size_t>(presetIndex)];
    return isGuidedLayout(preset) || isAdvancedLayout(preset);
}

bool UIManager::isBuiltInSenderPreset(int presetIndex) const {
    const auto& presets = Config::instance().getLayoutPresets();
    return presetIndex >= 0 && presetIndex < static_cast<int>(presets.size()) &&
           isCncLayout(presets[static_cast<std::size_t>(presetIndex)]);
}

bool UIManager::guidedExperienceSelected() const {
    return m_guidedExperienceGetter && m_guidedExperienceGetter();
}

int UIManager::workshopPresetIndex() const {
    const auto& presets = Config::instance().getLayoutPresets();
    const bool guided = guidedExperienceSelected();
    const int preferred = findPresetIndex(presets, [guided](const LayoutPreset& preset) {
        return guided ? isGuidedLayout(preset) : isAdvancedLayout(preset);
    });
    if (preferred >= 0)
        return preferred;
    return findPresetIndex(presets, [](const LayoutPreset& preset) {
        return isGuidedLayout(preset) || isAdvancedLayout(preset);
    });
}

int UIManager::senderPresetIndex() const {
    return findPresetIndex(Config::instance().getLayoutPresets(), isCncLayout);
}

void UIManager::selectGuidedExperience(bool guided) {
    if (m_cncStreaming)
        return;
    if (m_guidedExperienceSetter)
        m_guidedExperienceSetter(guided);
    if (m_workspaceMode == WorkspaceMode::Model)
        applyLayoutPreset(workshopPresetIndex());
}

LayoutPreset UIManager::captureCurrentLayout(const std::string& name) const {
    LayoutPreset preset;
    preset.name = name;

    for (const auto& entry : m_panelRegistry) {
        const auto* catalog = findWindowCatalogEntry(entry.key);
        if (catalog && !catalog->layoutPersistent)
            continue;
        preset.visibility[entry.key] = *entry.showFlag;
    }
    return preset;
}

void UIManager::saveCurrentAsPreset(const std::string& name) {
    auto& cfg = Config::instance();
    auto& presets = const_cast<std::vector<LayoutPreset>&>(cfg.getLayoutPresets());

    // Check for existing custom preset with same name — overwrite it
    for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
        auto idx = static_cast<size_t>(i);
        if (!presets[idx].builtIn && presets[idx].name == name) {
            cfg.updateLayoutPreset(i, captureCurrentLayout(name));
            m_activePresetIndex = i;
            cfg.setActiveLayoutPresetIndex(i);
            cfg.save();
            return;
        }
    }

    // New preset
    cfg.addLayoutPreset(captureCurrentLayout(name));
    m_activePresetIndex = static_cast<int>(cfg.getLayoutPresets().size()) - 1;
    cfg.setActiveLayoutPresetIndex(m_activePresetIndex);
    cfg.save();
}

void UIManager::deletePreset(int index) {
    auto& cfg = Config::instance();
    cfg.removeLayoutPreset(index);

    if (m_activePresetIndex >= static_cast<int>(cfg.getLayoutPresets().size()))
        m_activePresetIndex = static_cast<int>(cfg.getLayoutPresets().size()) - 1;
    cfg.setActiveLayoutPresetIndex(m_activePresetIndex);
    cfg.save();
}

void UIManager::checkAutoContextTrigger(const std::string& focusedPanelKey) {
    if (m_suppressAutoContext) return;

    auto& cfg = Config::instance();
    const auto& presets = cfg.getLayoutPresets();

    for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
        if (i == m_activePresetIndex) continue;
        if (presets[static_cast<size_t>(i)].autoTriggerPanelKey == focusedPanelKey) {
            const bool senderPreset = isBuiltInSenderPreset(i);
            const bool workshopPreset = isBuiltInWorkshopPreset(i);
            if ((senderPreset && m_workspaceMode == WorkspaceMode::CNC) ||
                (workshopPreset && m_workspaceMode == WorkspaceMode::Model)) {
                applyLayoutPreset(i);
                return;
            }
        }
    }
}

void UIManager::renderPresetSelector() {
    auto& cfg = Config::instance();
    const auto& presets = cfg.getLayoutPresets();

    const char* activeLabel = "Custom";
    if (m_activePresetIndex >= 0 &&
        m_activePresetIndex < static_cast<int>(presets.size())) {
        activeLabel = presets[static_cast<size_t>(m_activePresetIndex)].name.c_str();
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    const auto& style = ImGui::GetStyle();
    float comboWidth = ImGui::CalcTextSize(activeLabel).x + style.FramePadding.x * 4.0f +
                       ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(comboWidth);

    if (ImGui::BeginCombo("##LayoutPreset", activeLabel, ImGuiComboFlags_NoArrowButton)) {
        for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
            bool selected = (i == m_activePresetIndex);
            const bool disabled = (m_cncStreaming && !selected) ||
                                  (m_showLibrary && isBuiltInSenderPreset(i));
            if (disabled)
                ImGui::BeginDisabled();
            if (ImGui::Selectable(presets[static_cast<size_t>(i)].name.c_str(), selected))
                applyLayoutPreset(i);
            if (disabled) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip(
                        "The active layout stays fixed while a G-code stream is active.");
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }

        ImGui::Separator();
        if (ImGui::Selectable("Save Current Layout..."))
            m_showSavePresetPopup = true;
        if (m_activePresetIndex >= 0 &&
            m_activePresetIndex < static_cast<int>(presets.size()) &&
            !presets[static_cast<size_t>(m_activePresetIndex)].builtIn) {
            if (ImGui::Selectable("Delete Current Preset"))
                deletePreset(m_activePresetIndex);
        }
        ImGui::EndCombo();
    }
}

void UIManager::renderSavePresetPopup() {
    if (m_showSavePresetPopup) {
        ImGui::OpenPopup("Save Layout Preset");
        m_showSavePresetPopup = false;
    }
    if (ImGui::BeginPopupModal("Save Layout Preset", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Preset name:");
        float inputWidth = ImGui::CalcTextSize("M").x * 30.0f;
        ImGui::SetNextItemWidth(inputWidth);
        ImGui::InputText("##PresetName", m_presetNameBuf, sizeof(m_presetNameBuf));
        ImGui::Spacing();

        bool nameValid = m_presetNameBuf[0] != '\0';
        if (!nameValid) ImGui::BeginDisabled();
        if (ImGui::Button("Save")) {
            saveCurrentAsPreset(m_presetNameBuf);
            m_presetNameBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        if (!nameValid) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_presetNameBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace dw

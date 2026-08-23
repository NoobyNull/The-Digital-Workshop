// UIManager bridge for the persistent project shell. The widget receives one
// immutable presentation snapshot and emits only the Back to Project intent.

#include "managers/ui_manager.h"

#include <utility>

#include <imgui.h>

#include "core/config/config.h"
#include "modules/workshop/ui/project_context_bar.h"
#include "ui/dialogs/import_summary_dialog.h"
#include "ui/dialogs/message_dialog.h"
#include "ui/dialogs/progress_dialog.h"
#include "ui/dialogs/settings_import_dialog.h"
#include "ui/dialogs/tagger_shutdown_dialog.h"
#include "ui/panels/viewport_panel.h"
#include "ui/widgets/status_bar.h"
#include "ui/widgets/status_tips.h"
#include "ui/widgets/toast.h"

namespace dw {

void UIManager::renderDesignLibraryMenuItem() {
    const bool visible = isWindowVisible("library");
    if (!ImGui::MenuItem("Design Library", nullptr, visible))
        return;
    if (visible) {
        if (m_onCloseDesignLibrary)
            m_onCloseDesignLibrary();
    } else if (m_onOpenDesignLibrary) {
        m_onOpenDesignLibrary();
    }
}

void UIManager::renderProjectContextBar() {
    if (m_projectContextBar)
        m_projectContextBar->render();
}

float UIManager::projectContextBarHeight() const {
    return m_projectContextBar ? m_projectContextBar->height() : 0.0F;
}

void UIManager::setProjectShellSnapshot(workshop::ProjectShellSnapshot snapshot) {
    if (m_projectContextBar)
        m_projectContextBar->setSnapshot(std::move(snapshot));
}

void UIManager::setOnBackToProject(ActionCallback callback) {
    if (m_projectContextBar)
        m_projectContextBar->setBackToProjectCallback(std::move(callback));
}

void UIManager::renderBackgroundUI(float deltaTime, const LoadingState* loadingState) {
    if (m_statusBar) {
        m_statusBar->setContextTips(currentStatusTips());
        m_statusBar->render(loadingState);
    }
    if (m_progressDialog)
        m_progressDialog->render();

    MessageDialog::renderGlobal();
    SavePromptDialog::renderGlobal();
    ToastManager::instance().render(deltaTime);
    if (m_settingsImportDialog)
        m_settingsImportDialog->render();
}

std::vector<std::string> UIManager::currentStatusTips() const {
    auto& config = Config::instance();
    StatusTipState state;
    state.hasLoadedModel = m_viewportPanel != nullptr && m_viewportPanel->hasValidModel();
    state.cncConnected = m_cncConnected;
    state.cncStreaming = m_cncStreaming;
    state.lightDirection = config.getBinding(BindAction::LightDirDrag);
    state.lightIntensity = config.getBinding(BindAction::LightIntensityDrag);
    state.feedOverridePlus = config.getBinding(BindAction::FeedOverridePlus);
    state.feedOverrideMinus = config.getBinding(BindAction::FeedOverrideMinus);
    state.spindleOverridePlus = config.getBinding(BindAction::SpindleOverridePlus);
    state.spindleOverrideMinus = config.getBinding(BindAction::SpindleOverrideMinus);

    if (m_workspaceMode == WorkspaceMode::CNC)
        return buildStatusTips(StatusTipContext::Sender, state);
    if (m_showViewport)
        return buildStatusTips(StatusTipContext::WorkshopViewport, state);
    return buildStatusTips(StatusTipContext::Workshop, state);
}

void UIManager::setImportProgress(const ImportProgress* progress) {
    if (m_statusBar)
        m_statusBar->setImportProgress(progress);
}

void UIManager::setTaggerProgress(const TaggerProgress* progress) {
    if (m_statusBar)
        m_statusBar->setTaggerProgress(progress);
}

void UIManager::showImportSummary(const ImportBatchSummary& summary) {
    if (m_importSummaryDialog)
        m_importSummaryDialog->open(summary);
}

void UIManager::setImportCancelCallback(std::function<void()> callback) {
    if (m_statusBar)
        m_statusBar->setOnCancel(std::move(callback));
}

void UIManager::setTaggerCancelCallback(std::function<void()> callback) {
    if (m_statusBar)
        m_statusBar->setOnCancelTagging(std::move(callback));
}

void UIManager::showTaggerShutdownDialog(const TaggerProgress* progress) {
    if (m_taggerShutdownDialog)
        m_taggerShutdownDialog->open(progress);
}

} // namespace dw

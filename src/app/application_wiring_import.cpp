// Import queue, progress, options, and post-processing composition.

#include "app/application.h"

#include <string>
#include <vector>

#include "core/config/config.h"
#include "core/import/background_tagger.h"
#include "core/import/import_queue.h"
#include "core/threading/main_thread_queue.h"
#include "managers/file_io_manager.h"
#include "managers/ui_manager.h"
#include "ui/dialogs/import_options_dialog.h"
#include "ui/dialogs/import_summary_dialog.h"
#include "ui/widgets/toast.h"

namespace dw {

void Application::wireImportPipeline() {
    m_uiManager->setImportCancelCallback([this]() {
        if (m_importQueue)
            m_importQueue->cancel();
    });
    if (m_backgroundTagger) {
        m_uiManager->setTaggerProgress(&m_backgroundTagger->progress());
        m_uiManager->setTaggerCancelCallback([this]() {
            if (m_backgroundTagger && m_backgroundTagger->isActive()) {
                m_backgroundTagger->stop();
                ToastManager::instance().show(ToastType::Info,
                                              "AI Tagging",
                                              "Stopping after the current model");
            }
        });
    }
    m_importQueue->setOnBatchComplete([this](const ImportBatchSummary& summary) {
        m_mainThreadQueue->enqueue([this, summary]() {
            if (Config::instance().getShowImportErrorToasts()) {
                if (summary.failedCount > 0) {
                    ToastManager::instance().show(
                        ToastType::Error,
                        "Import Errors",
                        std::to_string(summary.failedCount) + " file(s) failed to import");
                }
                if (summary.successCount > 0) {
                    ToastManager::instance().show(
                        ToastType::Success,
                        "Import Complete",
                        std::to_string(summary.successCount) + " file(s) imported successfully");
                }
            }
            if (summary.duplicateCount > 0)
                m_uiManager->showImportSummary(summary);
            m_startAiTaggingAfterImportPostProcessing = m_importQueue->queueForTagging();
        });
    });
    m_fileIOManager->setImportPostProcessingCallback([this]() {
        if (!m_startAiTaggingAfterImportPostProcessing)
            return;
        m_startAiTaggingAfterImportPostProcessing = false;
        if (!m_backgroundTagger || m_backgroundTagger->isActive())
            return;
        std::string endpoint;
        std::string model;
        if (prepareAiTagging(endpoint, model))
            m_backgroundTagger->start(endpoint, model, BackgroundTaggerMode::SmartRetag);
    });
    m_fileIOManager->setImportOptionsDialog(m_uiManager->importOptionsDialog());
    if (m_uiManager->importOptionsDialog()) {
        m_uiManager->importOptionsDialog()->setOnConfirm(
            [this](FileHandlingMode mode, bool tagAfterImport, const std::vector<Path>& paths) {
                if (m_importQueue && !paths.empty()) {
                    m_importQueue->setQueueForTagging(tagAfterImport);
                    m_importQueue->enqueue(paths, mode);
                }
            });
        m_uiManager->importOptionsDialog()->setOnCancel(
            [this]() { m_pendingImportLibraryPurpose.reset(); });
        // The dialog never opens when the picker is cancelled or the
        // selection collects nothing; drop the pending purpose there too so
        // a later unrelated import isn't routed into the Start Project flow.
        m_fileIOManager->setOnImportSelectionAbandoned(
            [this]() { m_pendingImportLibraryPurpose.reset(); });
    }
    if (m_uiManager->importSummaryDialog()) {
        m_uiManager->importSummaryDialog()->setOnReimport(
            [this](std::vector<DuplicateRecord> selected) {
                if (m_importQueue && !selected.empty())
                    m_importQueue->enqueueForReimport(selected);
            });
    }
}

} // namespace dw

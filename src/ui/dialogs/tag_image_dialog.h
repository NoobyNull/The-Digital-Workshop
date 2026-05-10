#pragma once

#include <cstdint>
#include <functional>

#include <glad/gl.h>

#include "core/database/model_repository.h"
#include "core/materials/lmstudio_descriptor_service.h"
#include "ui/dialogs/dialog.h"

namespace dw {

class TagImageDialog : public Dialog {
  public:
    TagImageDialog();
    ~TagImageDialog() override = default;

    void render() override;

    // Open with model info + preloaded GL texture
    void open(const ModelRecord& record, GLuint thumbnailTexture);

    // Called from main thread queue when API result arrives
    void setResult(const DescriptorResult& result);

    // Callbacks
    using RequestCallback = std::function<void(int64_t modelId)>;
    using SaveCallback = std::function<void(int64_t modelId, const DescriptorResult& edited)>;
    void setOnRequest(RequestCallback cb) { m_onRequest = std::move(cb); }
    void setOnSave(SaveCallback cb) { m_onSave = std::move(cb); }

  private:
    enum class State { Idle, Loading, Ready };
    State m_state = State::Idle;

    ModelRecord m_record;
    GLuint m_thumbnailTexture = 0;

    // Editable buffers
    char m_titleBuf[256] = {};
    char m_description[2048] = {};
    char m_hover[1024] = {};
    char m_keywords[512] = {};
    char m_associations[512] = {};
    char m_categories[512] = {};
    std::string m_orientationSuggestion;

    // Error message (shown if API fails)
    std::string m_error;

    RequestCallback m_onRequest;
    SaveCallback m_onSave;

    // Build DescriptorResult from current buffer contents
    [[nodiscard]] DescriptorResult buildResult() const;
};

} // namespace dw

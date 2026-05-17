#include "tag_image_dialog.h"

#include <cstring>
#include <sstream>

#include <imgui.h>

#include "ui/ui_colors.h"
#include "ui/widgets/edit_buffer.h"

namespace dw {

namespace {

void renderLabeledInput(const char* label, char* buffer, size_t bufferSize, float width) {
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(width);
    std::string inputId = std::string("##") + label;
    ImGui::InputText(inputId.c_str(), buffer, bufferSize);
}

void renderLabeledMultiline(const char* label,
                            char* buffer,
                            size_t bufferSize,
                            const ImVec2& size) {
    ImGui::TextUnformatted(label);
    std::string inputId = std::string("##") + label;
    ImGui::InputTextMultiline(inputId.c_str(), buffer, bufferSize, size);
}

const char* thumbnailViewLabel(ThumbnailView view) {
    switch (view) {
    case ThumbnailView::Front: return "front";
    case ThumbnailView::Back: return "back";
    case ThumbnailView::Left: return "left";
    case ThumbnailView::Right: return "right";
    case ThumbnailView::Top: return "top";
    case ThumbnailView::Bottom: return "bottom";
    case ThumbnailView::Isometric: return "isometric";
    case ThumbnailView::Unknown: return "unknown";
    }
    return "unknown";
}

} // namespace

TagImageDialog::TagImageDialog() : Dialog("Tag Image") {}

void TagImageDialog::open(const ModelRecord& record, GLuint thumbnailTexture) {
    m_record = record;
    m_thumbnailTexture = thumbnailTexture;
    m_error.clear();

    // Clear buffers
    clearBuffer(m_titleBuf);
    clearBuffer(m_description);
    clearBuffer(m_hover);
    clearBuffer(m_keywords);
    clearBuffer(m_associations);
    clearBuffer(m_categories);
    m_orientationSuggestion.clear();

    m_state = State::Loading;
    m_open = true;

    // Fire the request callback
    if (m_onRequest) {
        m_onRequest(m_record.id);
    }
}

void TagImageDialog::setResult(const DescriptorResult& result) {
    if (!result.success) {
        m_error = result.error.empty() ? "Classification failed" : result.error;
        m_state = State::Ready;
        return;
    }

    m_error.clear();

    // Populate buffers from result
    fillBuffer(m_titleBuf, result.title);
    fillBuffer(m_description, result.description);
    fillBuffer(m_hover, result.hoverNarrative);

    // Join vectors as comma-separated
    auto join = [](const std::vector<std::string>& v) -> std::string {
        std::string out;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i > 0)
                out += ", ";
            out += v[i];
        }
        return out;
    };

    fillBuffer(m_keywords, join(result.keywords));
    fillBuffer(m_associations, join(result.associations));
    fillBuffer(m_categories, join(result.categories));

    if (result.orientation.needsRotation) {
        std::ostringstream suggestion;
        suggestion << "AI suggests rotating " << result.orientation.rotateDegrees
                   << " degrees clockwise";
        if (result.orientation.uprightView != ThumbnailView::Unknown)
            suggestion << " for " << thumbnailViewLabel(result.orientation.uprightView)
                       << " upright view";
        if (!result.orientation.reason.empty())
            suggestion << ": " << result.orientation.reason;
        m_orientationSuggestion = suggestion.str();
    } else {
        m_orientationSuggestion.clear();
    }

    m_state = State::Ready;
}

DescriptorResult TagImageDialog::buildResult() const {
    DescriptorResult r;
    r.success = true;
    r.title = m_titleBuf;
    r.description = m_description;
    r.hoverNarrative = m_hover;

    // Split comma-separated strings into vectors
    auto split = [](const char* buf) -> std::vector<std::string> {
        std::vector<std::string> out;
        std::istringstream ss(buf);
        std::string token;
        while (std::getline(ss, token, ',')) {
            // Trim whitespace
            size_t start = token.find_first_not_of(" \t");
            size_t end = token.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                out.push_back(token.substr(start, end - start + 1));
            }
        }
        return out;
    };

    r.keywords = split(m_keywords);
    r.associations = split(m_associations);
    r.categories = split(m_categories);
    return r;
}

void TagImageDialog::render() {
    if (!m_open) {
        return;
    }

    const auto* viewport = ImGui::GetMainViewport();
    ImVec2 tagDlgSize(viewport->WorkSize.x * 0.45f, viewport->WorkSize.y * 0.45f);
    ImGui::SetNextWindowSize(tagDlgSize, ImGuiCond_FirstUseEver);
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::Begin("Tag Image", &m_open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        if (!m_open) {
            m_state = State::Idle;
        }
        return;
    }

    bool isLoading = (m_state == State::Loading);
    bool isReady = (m_state == State::Ready);

    // --- Layout: thumbnail on left, fields on right ---
    float thumbSize = ImGui::GetFontSize() * 12;
    ImVec2 thumbUV0(0, 0);
    ImVec2 thumbUV1(1, 1);

    ImGui::BeginGroup();
    // Model name header
    ImGui::TextUnformatted(m_record.name.c_str());
    ImGui::Spacing();

    if (m_thumbnailTexture != 0) {
        ImGui::Image(static_cast<ImTextureID>(m_thumbnailTexture),
                     ImVec2(thumbSize, thumbSize),
                     thumbUV0,
                     thumbUV1);
    } else {
        ImGui::Dummy(ImVec2(thumbSize, thumbSize));
    }
    ImGui::EndGroup();

    ImGui::SameLine();

    // --- Right side: fields ---
    ImGui::BeginGroup();

    if (isLoading) {
        ImGui::TextDisabled("Classifying...");
        ImGui::Spacing();
    }

    if (!m_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, colors::kError);
        ImGui::TextWrapped("Error: %s", m_error.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // Disable fields while loading
    if (isLoading) {
        ImGui::BeginDisabled();
    }

    float fieldWidth = ImGui::GetContentRegionAvail().x;

    renderLabeledInput("Title", m_titleBuf, sizeof(m_titleBuf), fieldWidth);

    renderLabeledMultiline("Description",
                           m_description,
                           sizeof(m_description),
                           ImVec2(fieldWidth, ImGui::GetTextLineHeightWithSpacing() * 3));

    renderLabeledInput("Hover Tooltip", m_hover, sizeof(m_hover), fieldWidth);

    ImGui::Spacing();

    renderLabeledInput("Keywords / Tags", m_keywords, sizeof(m_keywords), fieldWidth);

    renderLabeledInput("Associations / References",
                       m_associations,
                       sizeof(m_associations),
                       fieldWidth);

    renderLabeledInput("Categories / Taxonomy",
                       m_categories,
                       sizeof(m_categories),
                       fieldWidth);

    if (!m_orientationSuggestion.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_orientationSuggestion.c_str());
    }

    if (isLoading) {
        ImGui::EndDisabled();
    }

    ImGui::EndGroup();

    // --- Buttons ---
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float tagBtnW = ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 4;
    if (isReady) {
        if (ImGui::Button("Save", ImVec2(tagBtnW, 0))) {
            if (m_onSave) {
                m_onSave(m_record.id, buildResult());
            }
            m_open = false;
            m_state = State::Idle;
        }
        ImGui::SameLine();
    }

    if (ImGui::Button("Cancel", ImVec2(tagBtnW, 0))) {
        m_open = false;
        m_state = State::Idle;
    }

    ImGui::End();
}

} // namespace dw

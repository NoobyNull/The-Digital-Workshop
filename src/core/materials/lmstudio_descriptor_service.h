#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dw {

enum class ThumbnailView {
    Front,
    Back,
    Left,
    Right,
    Top,
    Bottom,
    Isometric,
    Unknown,
};

enum class TagClassificationStatus {
    FinalTag,
    RetryView,
    FallbackIsometric,
    Unclassifiable,
};

enum class GeometryType {
    True3D,
    Relief25D,
    FlatPart,
    Unknown,
};

struct OrientationSuggestion {
    bool needsRotation = false;
    ThumbnailView uprightView = ThumbnailView::Unknown;
    int rotateDegrees = 0;
    std::string reason;
};

// Result of a descriptor request (AI classification of model thumbnail)
struct DescriptorResult {
    bool success = false;
    std::string error;
    TagClassificationStatus status = TagClassificationStatus::FinalTag;
    GeometryType geometryType = GeometryType::Unknown;
    float confidence = 0.0f;
    bool needsRetag = false;
    ThumbnailView currentView = ThumbnailView::Unknown;
    ThumbnailView recommendedView = ThumbnailView::Unknown;
    std::string viewReason;
    std::string title;
    std::string description;
    std::string hoverNarrative;
    std::vector<std::string> keywords;     // 3-5
    std::vector<std::string> associations; // brands/logos
    std::vector<std::string> categories;   // broad -> specific
    OrientationSuggestion orientation;
};

// Describes models via LM Studio image classification.
// All methods are blocking — call from a worker thread.
class LMStudioDescriptorService {
  public:
    DescriptorResult describe(const std::string& thumbnailPath,
                              const std::string& endpoint,
                              ThumbnailView currentView = ThumbnailView::Unknown);

    // Parse LM Studio JSON response into DescriptorResult. Public for deterministic parser tests.
    DescriptorResult parseClassification(const std::string& json);
    std::string extractClassificationJson(const std::string& responseJson);
    std::string classificationSystemPrompt() const;
    std::string classificationJsonSchema() const;

  private:
    // Convert model TGA thumbnail to PNG in-memory
    std::vector<uint8_t> tgaToPng(const std::string& tgaPath);

    // Send PNG image to LM Studio for classification, return raw JSON text
    std::string fetchClassification(const std::vector<uint8_t>& imageData,
                                    const std::string& endpoint,
                                    ThumbnailView currentView);

};

} // namespace dw

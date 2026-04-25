#include "properties_panel.h"

#include <imgui.h>

#include "../../core/paths/path_resolver.h"
#include "../../core/utils/string_utils.h"
#include "../icons.h"

namespace dw {

namespace {

double toDouble(float value) {
    return static_cast<double>(value);
}

} // namespace

PropertiesPanel::PropertiesPanel() : Panel("Properties") {}

void PropertiesPanel::render() {
    if (!m_open) {
        return;
    }

    applyMinSize(16, 10);
    if (ImGui::Begin(m_title.c_str(), &m_open)) {
        if (m_mesh && m_mesh->isValid()) {
            renderMeshInfo();
            ImGui::Spacing();
            renderBoundsInfo();
            ImGui::Spacing();
            renderTransformInfo();
            ImGui::Spacing();
            renderMaterialInfo();
        } else if (m_record) {
            renderModelRecordInfo();
        } else {
            ImGui::TextDisabled("No model selected");
            ImGui::TextDisabled("Select a model from the library");
        }
    }
    ImGui::End();
}

void PropertiesPanel::setMesh(std::shared_ptr<Mesh> mesh, const std::string& name) {
    m_mesh = std::move(mesh);
    m_meshName = name;
    m_record.reset();
}

void PropertiesPanel::setModelRecord(const ModelRecord& record) {
    m_record = record;
    m_mesh = nullptr;
    m_meshName.clear();
}

void PropertiesPanel::clearMesh() {
    m_mesh = nullptr;
    m_meshName.clear();
    m_record.reset();
}

void PropertiesPanel::renderModelRecordInfo() {
    const auto& r = *m_record;

    if (ImGui::CollapsingHeader("Model Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        ImGui::TextWrapped("Name: %s", r.name.c_str());
        ImGui::TextWrapped("Format: %s", r.fileFormat.c_str());

        ImGui::TextWrapped("File Size: %s", str::formatFileSize(r.fileSize).c_str());

        // Vertex count
        if (r.vertexCount >= 1000000) {
            ImGui::Text("Vertices: %.2fM", r.vertexCount / 1000000.0);
        } else if (r.vertexCount >= 1000) {
            ImGui::Text("Vertices: %.1fK", r.vertexCount / 1000.0);
        } else {
            ImGui::Text("Vertices: %u", r.vertexCount);
        }

        // Triangle count
        if (r.triangleCount >= 1000000) {
            ImGui::Text("Triangles: %.2fM", r.triangleCount / 1000000.0);
        } else if (r.triangleCount >= 1000) {
            ImGui::Text("Triangles: %.1fK", r.triangleCount / 1000.0);
        } else {
            ImGui::Text("Triangles: %u", r.triangleCount);
        }

        if (!r.importedAt.empty()) {
            ImGui::TextWrapped("Imported: %s", r.importedAt.c_str());
        }

        ImGui::Unindent();
    }

    // Bounds from record
    if (r.boundsMin.x != 0.0f || r.boundsMin.y != 0.0f || r.boundsMin.z != 0.0f ||
        r.boundsMax.x != 0.0f || r.boundsMax.y != 0.0f || r.boundsMax.z != 0.0f) {
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Bounds", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::TextWrapped(
                "Min: (%.3f, %.3f, %.3f)",
                toDouble(r.boundsMin.x),
                toDouble(r.boundsMin.y),
                toDouble(r.boundsMin.z));
            ImGui::TextWrapped(
                "Max: (%.3f, %.3f, %.3f)",
                toDouble(r.boundsMax.x),
                toDouble(r.boundsMax.y),
                toDouble(r.boundsMax.z));
            Vec3 size{r.boundsMax.x - r.boundsMin.x,
                      r.boundsMax.y - r.boundsMin.y,
                      r.boundsMax.z - r.boundsMin.z};
            ImGui::TextWrapped(
                "Size: %.3f x %.3f x %.3f",
                toDouble(size.x),
                toDouble(size.y),
                toDouble(size.z));
            ImGui::Unindent();
        }
    }

    // File path
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("File")) {
        ImGui::Indent();
        ImGui::TextWrapped("%s", PathResolver::resolve(r.filePath, PathCategory::Support).string().c_str());
        ImGui::Unindent();
    }

    // AI Classification (if available)
    renderAIClassification();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Double-click to load into viewport");
}

void PropertiesPanel::renderAIClassification() {
    if (!m_record) {
        return;
    }

    const auto& r = *m_record;

    // Only show section if any descriptor fields are populated
    if (r.descriptorTitle.empty() && r.descriptorDescription.empty()) {
        return;
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("AI Classification", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        // Title with hover tooltip
        if (!r.descriptorTitle.empty()) {
            ImGui::Text("Title:");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", r.descriptorTitle.c_str());
            if (!r.descriptorHover.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", r.descriptorHover.c_str());
            }
        }

        // Full description
        if (!r.descriptorDescription.empty()) {
            ImGui::Spacing();
            ImGui::Text("Description:");
            ImGui::TextWrapped("%s", r.descriptorDescription.c_str());
        }

        ImGui::Unindent();
    }
}

void PropertiesPanel::renderMeshInfo() {
    if (ImGui::CollapsingHeader("Mesh Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        // Name
        if (!m_meshName.empty()) {
            ImGui::TextWrapped("Name: %s", m_meshName.c_str());
        }

        // Vertex count
        size_t vertexCount = m_mesh->vertices().size();
        if (vertexCount >= 1000000) {
            ImGui::Text("Vertices: %.2fM", static_cast<double>(vertexCount) / 1000000.0);
        } else if (vertexCount >= 1000) {
            ImGui::Text("Vertices: %.1fK", static_cast<double>(vertexCount) / 1000.0);
        } else {
            ImGui::Text("Vertices: %zu", vertexCount);
        }

        // Triangle count
        size_t triangleCount = m_mesh->triangleCount();
        if (triangleCount >= 1000000) {
            ImGui::Text("Triangles: %.2fM", static_cast<double>(triangleCount) / 1000000.0);
        } else if (triangleCount >= 1000) {
            ImGui::Text("Triangles: %.1fK", static_cast<double>(triangleCount) / 1000.0);
        } else {
            ImGui::Text("Triangles: %zu", triangleCount);
        }

        // Index count
        size_t indexCount = m_mesh->indices().size();
        ImGui::Text("Indices: %zu", indexCount);

        // Memory usage estimate
        size_t memoryBytes = vertexCount * sizeof(Vertex) + indexCount * sizeof(uint32_t);
        ImGui::TextWrapped("Memory: %s", str::formatFileSize(memoryBytes).c_str());

        ImGui::Unindent();
    }
}

void PropertiesPanel::renderBoundsInfo() {
    if (ImGui::CollapsingHeader("Bounds", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        const AABB& bounds = m_mesh->bounds();

        ImGui::TextWrapped(
            "Min: (%.3f, %.3f, %.3f)",
            toDouble(bounds.min.x),
            toDouble(bounds.min.y),
            toDouble(bounds.min.z));
        ImGui::TextWrapped(
            "Max: (%.3f, %.3f, %.3f)",
            toDouble(bounds.max.x),
            toDouble(bounds.max.y),
            toDouble(bounds.max.z));

        Vec3 size = bounds.size();
        ImGui::TextWrapped(
            "Size: %.3f x %.3f x %.3f",
            toDouble(size.x),
            toDouble(size.y),
            toDouble(size.z));

        Vec3 center = bounds.center();
        ImGui::TextWrapped(
            "Center: (%.3f, %.3f, %.3f)",
            toDouble(center.x),
            toDouble(center.y),
            toDouble(center.z));

        float diagonal = bounds.diagonal();
        ImGui::TextWrapped("Diagonal: %.3f", toDouble(diagonal));

        ImGui::Unindent();
    }
}

void PropertiesPanel::renderTransformInfo() {
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        // Display current mesh bounds as informational readouts
        const AABB& bounds = m_mesh->bounds();
        Vec3 center = bounds.center();
        Vec3 size = bounds.size();

        // Position (center of bounding box) - read-only display
        float pos[3] = {center.x, center.y, center.z};
        ImGui::BeginDisabled();
        ImGui::DragFloat3("Center", pos, 0.1f);
        ImGui::EndDisabled();

        // Size - read-only display
        float sz[3] = {size.x, size.y, size.z};
        ImGui::BeginDisabled();
        ImGui::DragFloat3("Size", sz, 0.1f);
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // One-shot transform operations
        ImGui::Text("Operations");
        ImGui::Spacing();

        // Center on Origin button
        if (ImGui::Button("Center on Origin", ImVec2(-1, 0))) {
            m_mesh->centerOnOrigin();
            if (m_onMeshModified) {
                m_onMeshModified();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Move mesh so its center is at the origin (0, 0, 0)");
        }

        ImGui::Spacing();

        // Normalize Size button with configurable target size
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
        ImGui::DragFloat("Target Size", &m_targetSize, 0.1f, 0.01f, 1000.0f, "%.2f");

        if (ImGui::Button("Normalize Size", ImVec2(-1, 0))) {
            m_mesh->normalizeSize(m_targetSize);
            if (m_onMeshModified) {
                m_onMeshModified();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Scale mesh so its largest dimension equals the target size");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // One-shot translate, rotate, scale operations
        ImGui::Text("Apply Transform");
        ImGui::Spacing();

        // Translate
        ImGui::DragFloat3("Translate", m_translate, 0.1f);
        if (ImGui::Button("Apply##Translate", ImVec2(-1, 0))) {
            Mat4 mat = glm::translate(Mat4(1.0f),
                                      Vec3{m_translate[0], m_translate[1], m_translate[2]});
            m_mesh->transform(mat);
            m_translate[0] = m_translate[1] = m_translate[2] = 0.0f;
            if (m_onMeshModified) {
                m_onMeshModified();
            }
        }

        // Rotate (degrees)
        ImGui::DragFloat3("Rotate (deg)", m_rotateDeg, 1.0f);
        if (ImGui::Button("Apply##Rotate", ImVec2(-1, 0))) {
            constexpr float DEG2RAD = 3.14159265358979f / 180.0f;
            Mat4 mat = Mat4(1.0f);
            if (m_rotateDeg[0] != 0.0f)
                mat = glm::rotate(Mat4(1.0f), m_rotateDeg[0] * DEG2RAD, Vec3(1.0f, 0.0f, 0.0f)) *
                      mat;
            if (m_rotateDeg[1] != 0.0f)
                mat = glm::rotate(Mat4(1.0f), m_rotateDeg[1] * DEG2RAD, Vec3(0.0f, 1.0f, 0.0f)) *
                      mat;
            if (m_rotateDeg[2] != 0.0f)
                mat = glm::rotate(Mat4(1.0f), m_rotateDeg[2] * DEG2RAD, Vec3(0.0f, 0.0f, 1.0f)) *
                      mat;
            m_mesh->transform(mat);
            m_rotateDeg[0] = m_rotateDeg[1] = m_rotateDeg[2] = 0.0f;
            if (m_onMeshModified) {
                m_onMeshModified();
            }
        }

        // Scale
        ImGui::DragFloat3("Scale", m_scaleVal, 0.01f, 0.01f, 100.0f);
        if (ImGui::Button("Apply##Scale", ImVec2(-1, 0))) {
            Mat4 mat = glm::scale(Mat4(1.0f), Vec3{m_scaleVal[0], m_scaleVal[1], m_scaleVal[2]});
            m_mesh->transform(mat);
            m_scaleVal[0] = m_scaleVal[1] = m_scaleVal[2] = 1.0f;
            if (m_onMeshModified) {
                m_onMeshModified();
            }
        }

        ImGui::Unindent();
    }
}

void PropertiesPanel::renderMaterialInfo() {
    // If material is assigned, show material properties
    if (m_material) {
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            const auto& mat = m_material.value();

            // Material name header
            ImGui::Text("Name: %s", mat.name.c_str());

            // Category badge
            ImGui::Spacing();
            auto categoryStr = materialCategoryToString(mat.category);
            ImGui::Text("Category: %s", categoryStr.c_str());

            // Read-only material properties
            ImGui::Spacing();
            ImGui::Text("Properties");
            ImGui::Spacing();

            // Format Janka hardness as lbf
            ImGui::Text("Janka Hardness: %.0f lbf", toDouble(mat.jankaHardness));

            // Format feed rate as in/min
            ImGui::Text("Feed Rate: %.0f in/min", toDouble(mat.feedRate));

            // Format spindle speed as RPM
            ImGui::Text("Spindle Speed: %.0f RPM", toDouble(mat.spindleSpeed));

            // Format depth of cut as inches
            ImGui::Text("Depth of Cut: %.3f in", toDouble(mat.depthOfCut));

            // Grain direction slider (editable)
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float grainDir = mat.grainDirectionDeg;
            if (ImGui::SliderFloat("Grain Direction (deg)", &grainDir, 0.0f, 360.0f, "%.1f")) {
                m_material->grainDirectionDeg = grainDir;
                if (m_onGrainDirectionChanged) {
                    m_onGrainDirectionChanged(grainDir);
                }
            }

            // Action buttons
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Remove Material", ImVec2(-1, 0))) {
                clearMaterial();
                if (m_onMaterialRemoved) {
                    m_onMaterialRemoved();
                }
            }

            ImGui::Unindent();
        }
    } else {
        // No material assigned: show color picker as fallback
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            float color[3] = {m_objectColor.r, m_objectColor.g, m_objectColor.b};
            if (ImGui::ColorEdit3("Object Color",
                                  color,
                                  ImGuiColorEditFlags_NoInputs |
                                      ImGuiColorEditFlags_PickerHueWheel)) {
                m_objectColor.r = color[0];
                m_objectColor.g = color[1];
                m_objectColor.b = color[2];
                if (m_onColorChanged) {
                    m_onColorChanged(m_objectColor);
                }
            }

            // Color presets
            ImGui::Spacing();
            ImGui::Text("Presets");
            ImGui::Spacing();

            struct ColorPreset {
                const char* name;
                u32 hex;
            };

            static const ColorPreset presets[] = {
                {"Steel Blue", 0x6699CC},
                {"Silver", 0xC0C0C0},
                {"Gold", 0xDAA520},
                {"Copper", 0xB87333},
                {"Red", 0xCC3333},
                {"Green", 0x33CC33},
                {"White", 0xEEEEEE},
                {"Dark Gray", 0x555555},
            };

            for (int i = 0; i < 8; i++) {
                Color c = Color::fromHex(presets[i].hex);
                ImVec4 imColor(c.r, c.g, c.b, 1.0f);

                if (i > 0 && i % 4 != 0) {
                    ImGui::SameLine();
                }

                ImGui::PushID(i);
                float swatchSize = ImGui::GetFrameHeight();
                if (ImGui::ColorButton(presets[i].name, imColor, 0,
                        ImVec2(swatchSize, swatchSize))) {
                    m_objectColor = c;
                    if (m_onColorChanged) {
                        m_onColorChanged(m_objectColor);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", presets[i].name);
                }
                ImGui::PopID();
            }

            ImGui::Unindent();
        }
    }
}

} // namespace dw

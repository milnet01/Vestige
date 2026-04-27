// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gradient_editor_widget.cpp
/// @brief ImGui gradient editor widget implementation.
#include "editor/widgets/gradient_editor_widget.h"
#include "editor/widgets/color_gradient.h"

#include <imgui.h>

#include <algorithm>

namespace Vestige
{

/// @brief Size of stop marker triangles in pixels.
static constexpr float MARKER_SIZE = 8.0f;

bool drawGradientEditor(const char* label, ColorGradient& gradient, float width, float height)
{
    bool modified = false;

    ImGui::PushID(label);

    float barWidth = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    float totalHeight = height + MARKER_SIZE * 2.0f + 4.0f;  // bar + markers + padding

    ImVec2 barPos = ImGui::GetCursorScreenPos();
    barPos.y += MARKER_SIZE + 2.0f;  // Offset for top markers

    // Reserve space
    ImGui::InvisibleButton("##GradientBar",
                           ImVec2(barWidth, totalHeight),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool isHovered = ImGui::IsItemHovered();

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Draw checkerboard background (for alpha visibility)
    ImU32 checkA = IM_COL32(100, 100, 100, 255);
    ImU32 checkB = IM_COL32(60, 60, 60, 255);
    float checkSize = 8.0f;
    for (float cx = barPos.x; cx < barPos.x + barWidth; cx += checkSize)
    {
        for (float cy = barPos.y; cy < barPos.y + height; cy += checkSize)
        {
            int ix = static_cast<int>((cx - barPos.x) / checkSize);
            int iy = static_cast<int>((cy - barPos.y) / checkSize);
            ImU32 col = ((ix + iy) % 2 == 0) ? checkA : checkB;
            float x1 = std::min(cx + checkSize, barPos.x + barWidth);
            float y1 = std::min(cy + checkSize, barPos.y + height);
            drawList->AddRectFilled(ImVec2(cx, cy), ImVec2(x1, y1), col);
        }
    }

    // Draw gradient bar (sample at each pixel column)
    for (float x = 0.0f; x < barWidth; x += 1.0f)
    {
        float t = x / barWidth;
        glm::vec4 color = gradient.evaluate(t);
        ImU32 col = IM_COL32(
            static_cast<int>(color.r * 255.0f),
            static_cast<int>(color.g * 255.0f),
            static_cast<int>(color.b * 255.0f),
            static_cast<int>(color.a * 255.0f));
        drawList->AddRectFilled(
            ImVec2(barPos.x + x, barPos.y),
            ImVec2(barPos.x + x + 1.0f, barPos.y + height),
            col);
    }

    // Border
    drawList->AddRect(barPos,
                      ImVec2(barPos.x + barWidth, barPos.y + height),
                      IM_COL32(80, 80, 80, 255));

    // Phase 10.9 Slice 12 Ed9 — selected-stop and drag-stop state moved
    // out of file-static variables into per-widget storage via
    // `ImGui::GetStateStorage()`. Two gradient editors in the same panel
    // (e.g. emission gradient + tint gradient on a particle preset) used
    // to share the static slots through their gradientId qualifier; per-
    // widget storage scoped under the `PushID(label)` block keeps each
    // instance's selection and drag independent.
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID selectedKey = ImGui::GetID("##selectedStop");
    const ImGuiID dragKey     = ImGui::GetID("##dragStop");
    int selectedStop = storage->GetInt(selectedKey, -1);
    int dragStop     = storage->GetInt(dragKey, -1);

    // Draw stop markers and handle interaction
    ImVec2 mousePos = ImGui::GetMousePos();
    float markerY = barPos.y + height + 2.0f;

    for (int i = 0; i < static_cast<int>(gradient.stops.size()); ++i)
    {
        auto& stop = gradient.stops[static_cast<size_t>(i)];
        float markerX = barPos.x + stop.position * barWidth;

        // Marker triangle (pointing up at the bar)
        ImVec2 tri[3] = {
            ImVec2(markerX, markerY),
            ImVec2(markerX - MARKER_SIZE, markerY + MARKER_SIZE * 1.5f),
            ImVec2(markerX + MARKER_SIZE, markerY + MARKER_SIZE * 1.5f),
        };

        // Color fill for the marker
        ImU32 fillCol = IM_COL32(
            static_cast<int>(stop.color.r * 255.0f),
            static_cast<int>(stop.color.g * 255.0f),
            static_cast<int>(stop.color.b * 255.0f),
            255);
        drawList->AddTriangleFilled(tri[0], tri[1], tri[2], fillCol);

        // Border (highlight if selected)
        bool isSelected = (selectedStop == i);
        ImU32 borderCol = isSelected ? IM_COL32(255, 200, 50, 255) : IM_COL32(200, 200, 200, 255);
        drawList->AddTriangle(tri[0], tri[1], tri[2], borderCol, 2.0f);

        // Hit test on the marker area
        float dx = mousePos.x - markerX;
        float dy = mousePos.y - (markerY + MARKER_SIZE * 0.75f);
        bool overMarker = (std::abs(dx) < MARKER_SIZE && std::abs(dy) < MARKER_SIZE);

        // Click to select
        if (isHovered && overMarker && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            selectedStop = i;
            dragStop = i;
            storage->SetInt(selectedKey, selectedStop);
            storage->SetInt(dragKey,     dragStop);
        }

        // Drag to reposition
        bool isDragging = (dragStop == i);
        if (isDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            float newPos = std::clamp((mousePos.x - barPos.x) / barWidth, 0.0f, 1.0f);
            stop.position = newPos;
            modified = true;
        }

        if (isDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            dragStop = -1;
            storage->SetInt(dragKey, dragStop);
            gradient.sort();
            modified = true;
        }

        // Right-click to remove (keep at least 2 stops)
        if (isHovered && overMarker && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            if (gradient.stops.size() > 2)
            {
                gradient.removeStop(i);
                if (selectedStop >= static_cast<int>(gradient.stops.size()))
                {
                    selectedStop = static_cast<int>(gradient.stops.size()) - 1;
                    storage->SetInt(selectedKey, selectedStop);
                }
                modified = true;
                --i;
                continue;
            }
        }
    }

    // Double-click on bar to add new stop
    if (isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
        && dragStop == -1
        && mousePos.y >= barPos.y && mousePos.y <= barPos.y + height)
    {
        float newPos = std::clamp((mousePos.x - barPos.x) / barWidth, 0.0f, 1.0f);
        glm::vec4 newColor = gradient.evaluate(newPos);
        gradient.addStop(newPos, newColor);
        selectedStop = -1;  // Will be found on next frame
        storage->SetInt(selectedKey, selectedStop);
        modified = true;
    }

    // Color picker for selected stop
    if (selectedStop >= 0
        && selectedStop < static_cast<int>(gradient.stops.size()))
    {
        auto& stop = gradient.stops[static_cast<size_t>(selectedStop)];
        ImGui::Spacing();
        ImGui::Text("Stop %d (pos: %.2f)", selectedStop,
                    static_cast<double>(stop.position));
        if (ImGui::ColorEdit4("##StopColor", &stop.color.x,
                              ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
        {
            modified = true;
        }
    }

    // Label
    ImGui::TextDisabled("%s  (dbl-click: add, right-click: remove)", label);

    ImGui::PopID();
    return modified;
}

} // namespace Vestige

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file curve_editor_widget.cpp
/// @brief ImGui curve editor widget implementation.
#include "editor/widgets/curve_editor_widget.h"
#include "editor/widgets/animation_curve.h"

#include <imgui.h>

#include <algorithm>

namespace Vestige
{

/// @brief Size of keyframe grab handles in pixels.
static constexpr float HANDLE_RADIUS = 5.0f;

/// @brief Converts curve-space (0-1, 0-1) to pixel-space within the widget.
static ImVec2 curveToPixel(float t, float v, const ImVec2& origin, const ImVec2& size)
{
    return ImVec2(
        origin.x + t * size.x,
        origin.y + (1.0f - v) * size.y  // Y is inverted (ImGui top-left origin)
    );
}

/// @brief Converts pixel-space to curve-space (0-1, 0-1), clamped.
static void pixelToCurve(const ImVec2& pixel, const ImVec2& origin, const ImVec2& size,
                         float& outT, float& outV)
{
    outT = std::clamp((pixel.x - origin.x) / size.x, 0.0f, 1.0f);
    outV = std::clamp(1.0f - (pixel.y - origin.y) / size.y, 0.0f, 1.0f);
}

bool drawCurveEditor(const char* label, AnimationCurve& curve, float width, float height)
{
    bool modified = false;

    ImGui::PushID(label);

    // Calculate widget region
    ImVec2 canvasSize(width > 0.0f ? width : ImGui::GetContentRegionAvail().x, height);
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();

    // Reserve space in the layout
    ImGui::InvisibleButton("##CurveCanvas", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool isHovered = ImGui::IsItemHovered();

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Background
    ImU32 bgColor = IM_COL32(40, 40, 40, 255);
    ImU32 gridColor = IM_COL32(60, 60, 60, 255);
    ImU32 lineColor = IM_COL32(120, 200, 120, 255);
    ImU32 handleColor = IM_COL32(255, 255, 255, 255);
    ImU32 handleSelectedColor = IM_COL32(255, 200, 50, 255);
    ImU32 borderColor = IM_COL32(80, 80, 80, 255);

    drawList->AddRectFilled(canvasPos,
                            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                            bgColor);
    drawList->AddRect(canvasPos,
                      ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                      borderColor);

    // Grid lines (4 horizontal, 4 vertical)
    for (int i = 1; i < 4; ++i)
    {
        float fx = canvasPos.x + canvasSize.x * (static_cast<float>(i) / 4.0f);
        float fy = canvasPos.y + canvasSize.y * (static_cast<float>(i) / 4.0f);
        drawList->AddLine(ImVec2(fx, canvasPos.y),
                          ImVec2(fx, canvasPos.y + canvasSize.y), gridColor);
        drawList->AddLine(ImVec2(canvasPos.x, fy),
                          ImVec2(canvasPos.x + canvasSize.x, fy), gridColor);
    }

    // Draw the curve line (sample at many points for smooth rendering)
    constexpr int NUM_SAMPLES = 64;
    for (int i = 0; i < NUM_SAMPLES; ++i)
    {
        float t0 = static_cast<float>(i) / static_cast<float>(NUM_SAMPLES);
        float t1 = static_cast<float>(i + 1) / static_cast<float>(NUM_SAMPLES);
        float v0 = curve.evaluate(t0);
        float v1 = curve.evaluate(t1);

        ImVec2 p0 = curveToPixel(t0, v0, canvasPos, canvasSize);
        ImVec2 p1 = curveToPixel(t1, v1, canvasPos, canvasSize);
        drawList->AddLine(p0, p1, lineColor, 2.0f);
    }

    // Phase 10.9 Slice 12 Ed9 — drag state moved out of file-static
    // variables and into per-widget storage via `ImGui::GetStateStorage()`.
    // The previous static + widgetId qualifier worked when the curve
    // editor only ever rendered one instance per frame, but a nested or
    // duplicated invocation (two curve widgets in the same panel) would
    // share the static slots and conflict on drag start. Per-widget
    // storage is keyed by IDs derived inside this PushID(label) block
    // so each instance gets its own state.
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID dragIndexKey = ImGui::GetID("##dragIndex");
    int dragIndex = storage->GetInt(dragIndexKey, -1);

    // Draw handles and handle interaction
    ImVec2 mousePos = ImGui::GetMousePos();
    for (int i = 0; i < static_cast<int>(curve.keyframes.size()); ++i)
    {
        auto& kf = curve.keyframes[static_cast<size_t>(i)];
        ImVec2 handlePos = curveToPixel(kf.time, kf.value, canvasPos, canvasSize);

        float dx = mousePos.x - handlePos.x;
        float dy = mousePos.y - handlePos.y;
        bool overHandle = (dx * dx + dy * dy) <= (HANDLE_RADIUS + 3.0f) * (HANDLE_RADIUS + 3.0f);

        bool isDragging = (dragIndex == i);

        // Start drag
        if (isHovered && overHandle && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            dragIndex = i;
            storage->SetInt(dragIndexKey, dragIndex);
        }

        // Continue drag
        if (isDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            float newT = 0.0f;
            float newV = 0.0f;
            pixelToCurve(mousePos, canvasPos, canvasSize, newT, newV);

            // First and last keyframes are pinned to t=0 and t=1
            if (i == 0)
            {
                newT = 0.0f;
            }
            else if (i == static_cast<int>(curve.keyframes.size()) - 1)
            {
                newT = 1.0f;
            }

            kf.time = newT;
            kf.value = newV;
            modified = true;
        }

        // End drag
        if (isDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            dragIndex = -1;
            storage->SetInt(dragIndexKey, dragIndex);
            curve.sort();
            modified = true;
        }

        // Right-click to delete (middle keyframes only)
        if (isHovered && overHandle && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            if (i > 0 && i < static_cast<int>(curve.keyframes.size()) - 1)
            {
                curve.removeKeyframe(i);
                modified = true;
                --i;  // Adjust index after removal
                continue;
            }
        }

        // Draw handle
        ImU32 color = isDragging ? handleSelectedColor : handleColor;
        drawList->AddCircleFilled(handlePos, HANDLE_RADIUS, color);
        drawList->AddCircle(handlePos, HANDLE_RADIUS, borderColor);
    }

    // Double-click to add new keyframe
    if (isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && dragIndex == -1)
    {
        float newT = 0.0f;
        float newV = 0.0f;
        pixelToCurve(mousePos, canvasPos, canvasSize, newT, newV);
        curve.addKeyframe(newT, newV);
        modified = true;
    }

    // Tooltip showing value at cursor position
    if (isHovered && dragIndex == -1)
    {
        float hoverT = 0.0f;
        float hoverV = 0.0f;
        pixelToCurve(mousePos, canvasPos, canvasSize, hoverT, hoverV);
        float curveVal = curve.evaluate(hoverT);
        ImGui::SetTooltip("t=%.2f  v=%.2f", static_cast<double>(hoverT),
                          static_cast<double>(curveVal));
    }

    // Label
    ImGui::TextDisabled("%s  (dbl-click: add, right-click: remove)", label);

    ImGui::PopID();
    return modified;
}

} // namespace Vestige

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/ui_world_projection.h"

namespace Vestige
{

WorldToScreenResult projectWorldToScreen(const glm::vec3& worldPos,
                                          const glm::mat4& viewProj,
                                          int screenWidth, int screenHeight)
{
    WorldToScreenResult out{};
    out.visible  = false;
    out.screenPos = {0.0f, 0.0f};
    out.ndcDepth  = 0.0f;

    const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0f) return out;  // Behind the camera.

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.x < -1.0f || ndc.x > 1.0f ||
        ndc.y < -1.0f || ndc.y > 1.0f ||
        ndc.z < -1.0f || ndc.z >  1.0f)
    {
        return out;  // Outside the frustum.
    }

    out.visible = true;
    out.ndcDepth = ndc.z;
    out.screenPos = {
        (ndc.x * 0.5f + 0.5f) * static_cast<float>(screenWidth),
        // Y flip: NDC Y is bottom-up, screen Y is top-down.
        (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(screenHeight),
    };
    return out;
}

} // namespace Vestige

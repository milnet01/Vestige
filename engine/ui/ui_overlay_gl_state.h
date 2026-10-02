// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_overlay_gl_state.h
/// @brief The GL state the 2D UI overlay draws under, set for one scope and
///        restored after it.
#pragma once

#include <glad/gl.h>

namespace Vestige
{

/// @brief Sets the overlay's GL state for its lifetime: no depth test,
///        alpha blending, and no face culling.
///
/// Culling matters (3D_E-0747): the overlay's top-left-origin projection
/// makes every sprite quad clockwise, so the 3D pass's back-face culling,
/// left enabled, discards the whole overlay. The previous depth, blend and
/// cull switches come back on destruction; the blend function does not.
class UIOverlayGlState
{
public:
    UIOverlayGlState()
    {
        glGetBooleanv(GL_DEPTH_TEST, &m_depth);
        glGetBooleanv(GL_BLEND, &m_blend);
        glGetBooleanv(GL_CULL_FACE, &m_cull);

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
    }

    ~UIOverlayGlState()
    {
        restore(GL_DEPTH_TEST, m_depth);
        restore(GL_BLEND, m_blend);
        restore(GL_CULL_FACE, m_cull);
    }

    UIOverlayGlState(const UIOverlayGlState&) = delete;
    UIOverlayGlState& operator=(const UIOverlayGlState&) = delete;
    UIOverlayGlState(UIOverlayGlState&&) = delete;
    UIOverlayGlState& operator=(UIOverlayGlState&&) = delete;

private:
    static void restore(GLenum cap, GLboolean wasEnabled)
    {
        if (wasEnabled == GL_TRUE)
        {
            glEnable(cap);
        }
        else
        {
            glDisable(cap);
        }
    }

    GLboolean m_depth = GL_FALSE;
    GLboolean m_blend = GL_FALSE;
    GLboolean m_cull  = GL_FALSE;
};

} // namespace Vestige

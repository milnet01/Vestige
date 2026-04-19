// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file scoped_forward_z.h
/// @brief RAII helper that switches the pipeline to standard forward-Z for a
/// scope and restores the caller's previous clip / depth state on exit.
#pragma once

#include <glad/gl.h>

namespace Vestige
{

/// @brief Flips clipControl + depthFunc + clearDepth from the engine's global
/// reverse-Z (`GL_ZERO_TO_ONE` + `GL_GEQUAL` + clearDepth 0.0) to forward-Z
/// (`GL_NEGATIVE_ONE_TO_ONE` + `GL_LESS` + clearDepth 1.0) for passes that
/// require it (cubemap captures, shadow maps, light-probe bakes), then
/// restores on destruction.
///
/// `glClipControl` is global state — a pass that forgets to restore it
/// silently breaks every subsequent reverse-Z draw. This helper makes the
/// save/restore exception- and early-return-safe.
class ScopedForwardZ
{
public:
    ScopedForwardZ()
    {
        glGetIntegerv(GL_CLIP_ORIGIN, &m_prevClipOrigin);
        glGetIntegerv(GL_CLIP_DEPTH_MODE, &m_prevClipDepth);
        glGetIntegerv(GL_DEPTH_FUNC, &m_prevDepthFunc);
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &m_prevClearDepth);

        glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
        glDepthFunc(GL_LESS);
        glClearDepth(1.0);
    }

    ~ScopedForwardZ()
    {
        glClipControl(static_cast<GLenum>(m_prevClipOrigin),
                      static_cast<GLenum>(m_prevClipDepth));
        glDepthFunc(static_cast<GLenum>(m_prevDepthFunc));
        glClearDepth(static_cast<GLdouble>(m_prevClearDepth));
    }

    ScopedForwardZ(const ScopedForwardZ&) = delete;
    ScopedForwardZ& operator=(const ScopedForwardZ&) = delete;
    ScopedForwardZ(ScopedForwardZ&&) = delete;
    ScopedForwardZ& operator=(ScopedForwardZ&&) = delete;

private:
    GLint m_prevClipOrigin = 0;
    GLint m_prevClipDepth = 0;
    GLint m_prevDepthFunc = 0;
    GLfloat m_prevClearDepth = 0.0f;
};

} // namespace Vestige

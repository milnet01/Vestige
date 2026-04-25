// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "renderer/scoped_blend_state.h"

namespace Vestige
{

BlendStateGlIo::SavedState BlendStateGlIo::save()
{
    SavedState s;
    s.enabled = (glIsEnabled(GL_BLEND) == GL_TRUE);

    GLint val = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB,   &val); s.srcRgb   = static_cast<GLenum>(val);
    glGetIntegerv(GL_BLEND_DST_RGB,   &val); s.dstRgb   = static_cast<GLenum>(val);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &val); s.srcAlpha = static_cast<GLenum>(val);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &val); s.dstAlpha = static_cast<GLenum>(val);

    return s;
}

void BlendStateGlIo::apply(bool enable, GLenum srcFactor, GLenum dstFactor)
{
    if (enable)
    {
        glEnable(GL_BLEND);
    }
    else
    {
        glDisable(GL_BLEND);
    }
    glBlendFunc(srcFactor, dstFactor);
}

void BlendStateGlIo::restore(const SavedState& saved)
{
    if (saved.enabled)
    {
        glEnable(GL_BLEND);
    }
    else
    {
        glDisable(GL_BLEND);
    }
    glBlendFuncSeparate(saved.srcRgb, saved.dstRgb,
                        saved.srcAlpha, saved.dstAlpha);
}

} // namespace Vestige

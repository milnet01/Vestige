// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "renderer/scoped_shadow_depth_state.h"

namespace Vestige
{

ShadowDepthGlIo::SavedState ShadowDepthGlIo::save()
{
    SavedState s;
    s.clipDistance0 = (glIsEnabled(GL_CLIP_DISTANCE0) == GL_TRUE);
    s.depthClamp    = (glIsEnabled(GL_DEPTH_CLAMP)    == GL_TRUE);
    return s;
}

void ShadowDepthGlIo::applyShadowState()
{
    glDisable(GL_CLIP_DISTANCE0);
    glEnable(GL_DEPTH_CLAMP);
}

void ShadowDepthGlIo::restore(const SavedState& saved)
{
    if (saved.clipDistance0)
    {
        glEnable(GL_CLIP_DISTANCE0);
    }
    else
    {
        glDisable(GL_CLIP_DISTANCE0);
    }

    if (saved.depthClamp)
    {
        glEnable(GL_DEPTH_CLAMP);
    }
    else
    {
        glDisable(GL_DEPTH_CLAMP);
    }
}

} // namespace Vestige

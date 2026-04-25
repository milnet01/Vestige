// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "renderer/scoped_cull_face.h"

namespace Vestige
{

CullFaceGlIo::SavedState CullFaceGlIo::save()
{
    SavedState s;
    s.enabled = (glIsEnabled(GL_CULL_FACE) == GL_TRUE);
    return s;
}

void CullFaceGlIo::apply(bool enable)
{
    if (enable)
    {
        glEnable(GL_CULL_FACE);
    }
    else
    {
        glDisable(GL_CULL_FACE);
    }
}

void CullFaceGlIo::restore(const SavedState& saved)
{
    if (saved.enabled)
    {
        glEnable(GL_CULL_FACE);
    }
    else
    {
        glDisable(GL_CULL_FACE);
    }
}

} // namespace Vestige

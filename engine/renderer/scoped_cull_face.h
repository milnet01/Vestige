// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file scoped_cull_face.h
/// @brief RAII guard for `GL_CULL_FACE` enable state.
///
/// The foliage / particle / sky paths disable face culling so two-
/// sided geometry (grass, transparent billboards, skybox cube)
/// renders correctly, then re-enable it on the way out. The
/// re-enable assumes the global default of "cull on" was in place
/// when the pass started — fragile under cross-pass composition
/// (e.g. the editor's debug-draw mode runs with cull off, and any
/// foliage call inside that mode incorrectly turns cull back on).
/// The RAII below saves the prior enable bit and restores it on
/// destruction so the pass is composable.
///
/// Same pattern as Phase 10.9 R3's `ScopedShadowDepthState` and
/// R4's `ScopedBlendState` — template-injectable IO so the bracket
/// contract is unit-testable without a GL context.
#pragma once

#include <glad/gl.h>

namespace Vestige
{

/// @brief State-IO interface used by `ScopedCullFaceImpl`.
struct CullFaceGlIo
{
    struct SavedState
    {
        bool enabled = false;
    };

    static SavedState save();
    static void apply(bool enable);
    static void restore(const SavedState& saved);
};

/// @brief RAII bracket for `GL_CULL_FACE` enable.
template <typename Io = CullFaceGlIo>
class ScopedCullFaceImpl
{
public:
    explicit ScopedCullFaceImpl(bool enable)
        : m_saved(Io::save())
    {
        Io::apply(enable);
    }

    ~ScopedCullFaceImpl()
    {
        Io::restore(m_saved);
    }

    ScopedCullFaceImpl(const ScopedCullFaceImpl&) = delete;
    ScopedCullFaceImpl& operator=(const ScopedCullFaceImpl&) = delete;
    ScopedCullFaceImpl(ScopedCullFaceImpl&&) = delete;
    ScopedCullFaceImpl& operator=(ScopedCullFaceImpl&&) = delete;

private:
    typename Io::SavedState m_saved;
};

using ScopedCullFace = ScopedCullFaceImpl<CullFaceGlIo>;

} // namespace Vestige

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file scoped_blend_state.h
/// @brief RAII guard for `GL_BLEND` enable + blend factor state.
///
/// Several render passes (foliage, water, tree, particle) toggle
/// `glEnable(GL_BLEND)` + `glBlendFunc(...)` and rely on the global
/// default of "blend off" being in place when control returns to
/// the caller. That assumption holds today because no other
/// long-running pass leaves blend enabled; it stops holding the
/// moment one does. This RAII saves the previous enable bit + the
/// per-channel factors and restores them on destruction so each
/// subsystem stays composable with any caller state.
///
/// Same pattern as Phase 10.9 R3's `ScopedShadowDepthState` —
/// template-injectable IO so the bracket contract is unit-testable
/// without a GL context.
#pragma once

#include <glad/gl.h>

namespace Vestige
{

/// @brief State-IO interface used by `ScopedBlendStateImpl`.
struct BlendStateGlIo
{
    struct SavedState
    {
        bool enabled = false;
        GLenum srcRgb = GL_ONE;
        GLenum dstRgb = GL_ZERO;
        GLenum srcAlpha = GL_ONE;
        GLenum dstAlpha = GL_ZERO;
    };

    static SavedState save();
    static void apply(bool enable, GLenum srcFactor, GLenum dstFactor);
    static void restore(const SavedState& saved);
};

/// @brief RAII bracket for blend state.
///
/// Constructor parameters describe the *desired* state to apply
/// inside the scope. The caller's prior state is snapshotted before
/// the apply and restored on destruction.
template <typename Io = BlendStateGlIo>
class ScopedBlendStateImpl
{
public:
    ScopedBlendStateImpl(bool enable, GLenum srcFactor, GLenum dstFactor)
        : m_saved(Io::save())
    {
        Io::apply(enable, srcFactor, dstFactor);
    }

    ~ScopedBlendStateImpl()
    {
        Io::restore(m_saved);
    }

    ScopedBlendStateImpl(const ScopedBlendStateImpl&) = delete;
    ScopedBlendStateImpl& operator=(const ScopedBlendStateImpl&) = delete;
    ScopedBlendStateImpl(ScopedBlendStateImpl&&) = delete;
    ScopedBlendStateImpl& operator=(ScopedBlendStateImpl&&) = delete;

private:
    typename Io::SavedState m_saved;
};

using ScopedBlendState = ScopedBlendStateImpl<BlendStateGlIo>;

} // namespace Vestige

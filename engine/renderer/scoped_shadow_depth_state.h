// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file scoped_shadow_depth_state.h
/// @brief RAII guard for the shadow-pass-specific GL state
/// (`GL_CLIP_DISTANCE0` + `GL_DEPTH_CLAMP`).
///
/// `ScopedForwardZ` (sibling header) handles the clip-mode + depth-
/// function flip the shadow pass needs (forward-Z + GL_LESS); this
/// header handles the per-shadow-pass enable/disable bits:
/// `GL_CLIP_DISTANCE0` must be off (water reflection / refraction
/// passes from a previous frame may have left it on, and shaders
/// that don't write `gl_ClipDistance[0]` produce undefined clip
/// values when it's enabled), and `GL_DEPTH_CLAMP` must be on
/// (keeps shadow casters in front of the near plane clamped to
/// depth 0 instead of clipped — avoids shadow pancaking).
///
/// Before R3, both bits were toggled with bare `glDisable` /
/// `glEnable` calls inside `renderShadowPass`, with no save of
/// the caller's prior state. This RAII brackets the toggle so a
/// caller that had `GL_CLIP_DISTANCE0` enabled (water passes) or
/// `GL_DEPTH_CLAMP` enabled (a future caller) sees its state
/// preserved across the shadow pass.
#pragma once

#include <glad/gl.h>

namespace Vestige
{

/// @brief State-IO interface used by `ScopedShadowDepthStateImpl`.
///
/// Production callers use the GL implementation directly (the
/// `ScopedShadowDepthState` typedef below). Tests inject a mock IO
/// type with the same shape so the bracket contract is testable
/// without a GL context — same pattern as Phase 10.9 R1's
/// `runIblCaptureSequenceWith<Guard>`.
struct ShadowDepthGlIo
{
    struct SavedState
    {
        bool clipDistance0 = false;
        bool depthClamp = false;
    };

    /// Snapshot the relevant enable bits.
    static SavedState save();
    /// Apply the shadow-pass-specific values: `GL_CLIP_DISTANCE0`
    /// off, `GL_DEPTH_CLAMP` on.
    static void applyShadowState();
    /// Restore the saved values.
    static void restore(const SavedState& saved);
};

/// @brief RAII bracket: snapshots state on construction, applies
/// shadow-pass state, restores on destruction. Templated on
/// `Io` so tests can inject a recording mock; production uses
/// `ShadowDepthGlIo`.
template <typename Io = ShadowDepthGlIo>
class ScopedShadowDepthStateImpl
{
public:
    ScopedShadowDepthStateImpl()
        : m_saved(Io::save())
    {
        Io::applyShadowState();
    }

    ~ScopedShadowDepthStateImpl()
    {
        // RED stub — restore intentionally absent. The shadow pass
        // therefore leaks GL_CLIP_DISTANCE0 / GL_DEPTH_CLAMP state
        // across the function boundary, the exact bug R3 is supposed
        // to close. The green commit replaces this with the correct
        // `Io::restore(m_saved);` call.
    }

    ScopedShadowDepthStateImpl(const ScopedShadowDepthStateImpl&) = delete;
    ScopedShadowDepthStateImpl& operator=(const ScopedShadowDepthStateImpl&) = delete;
    ScopedShadowDepthStateImpl(ScopedShadowDepthStateImpl&&) = delete;
    ScopedShadowDepthStateImpl& operator=(ScopedShadowDepthStateImpl&&) = delete;

private:
    typename Io::SavedState m_saved;
};

/// @brief Production typedef — uses the GL backend.
using ScopedShadowDepthState = ScopedShadowDepthStateImpl<ShadowDepthGlIo>;

} // namespace Vestige

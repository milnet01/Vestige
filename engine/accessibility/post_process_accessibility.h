// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file post_process_accessibility.h
/// @brief Phase 10 accessibility — post-process effect toggles that
///        motion-sensitive users need to disable (depth-of-field
///        blur, full-screen motion blur).
///
/// *Why a separate module, and why now?*  The effects themselves
/// (DoF, motion blur) land later in the Phase 10 Post-Processing
/// Effects Suite. Shipping the toggles before the effects means:
///
///   1. The settings UI + persistence layer can ship with the full
///      accessibility preset already wired, so the user's
///      preferences survive the moment the effects appear.
///   2. When each effect lands it reads a single boolean from a
///      canonical place — no scramble to find a home for the flag
///      on the day the feature merges.
///   3. The accessibility-preset concept (one toggle that makes
///      every motion-sensitive effect safe at once) has a real
///      type to hang off — calling `safeDefaults()` returns the
///      exact struct the settings screen's "Accessibility preset"
///      button applies.
///
/// Both effects default to **enabled** because they are normal
/// gameplay polish. Users opt into the disabled state either by
/// toggling individually or by applying `safeDefaults()`.
///
/// References: WCAG 2.2 SC 2.3.3 ("Animation from Interactions" —
/// users must be able to disable non-essential motion); Xbox /
/// Ubisoft accessibility guidelines ("camera-blur effects should
/// be opt-out"); Game Accessibility Guidelines (gameaccessibility
/// guidelines.com) "Avoid motion blur; allow it to be turned off".
#pragma once

namespace Vestige
{

/// @brief Motion-sensitive post-process toggles.
struct PostProcessAccessibilitySettings
{
    /// Depth-of-field blur (focal-plane sharpness, everything else
    /// blurred). Can induce nausea in motion-sensitive users —
    /// surfaced as a dedicated Settings toggle per the Phase 10
    /// accessibility roadmap.
    bool depthOfFieldEnabled = true;

    /// Full-screen motion blur (per-pixel / per-object velocity
    /// smearing). One of the most-requested accessibility toggles
    /// in every mainstream-game accessibility study for the last
    /// decade — off-by-default is recommended by the Game
    /// Accessibility Guidelines working group but Vestige defaults
    /// on for visual quality, leaving the toggle to users.
    bool motionBlurEnabled    = true;

    /// Distance / height fog master toggle. Fog stays on by default
    /// because disabling it produces a harsh "fog horizon" cutoff on
    /// long sightlines (visually worse than having fog for most
    /// users). Exposed so users with extreme low-contrast sensitivity
    /// can opt out entirely.
    bool fogEnabled = true;

    /// Density-scale multiplier applied to distance + height fog so
    /// low-vision users can see at distance without losing the look
    /// entirely. 1.0 = full authored density; 0.0 = no fog.
    float fogIntensityScale = 1.0f;

    /// Reduced-motion mode for fog-related effects: disables temporal
    /// reprojection in volumetric fog (when that feature ships) and
    /// caps the sun-inscatter lobe intensity so rapid camera pans
    /// past the sun don't flash. Distance + height fog remain
    /// unaffected (they're static per frame).
    bool reduceMotionFog = false;

    /// Value equality — two configs match iff every flag matches.
    bool operator==(const PostProcessAccessibilitySettings& o) const
    {
        return depthOfFieldEnabled == o.depthOfFieldEnabled
            && motionBlurEnabled    == o.motionBlurEnabled
            && fogEnabled           == o.fogEnabled
            && fogIntensityScale    == o.fogIntensityScale
            && reduceMotionFog      == o.reduceMotionFog;
    }
    bool operator!=(const PostProcessAccessibilitySettings& o) const { return !(*this == o); }
};

/// @brief Returns the "Accessibility preset" — every motion-sensitive
///        post-process effect disabled.
///
/// Used by the Settings screen's one-click accessibility preset
/// button so a user who needs the safest possible configuration
/// can apply it without toggling each flag individually.
PostProcessAccessibilitySettings safeDefaults();

} // namespace Vestige

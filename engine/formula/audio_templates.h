// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_templates.h
/// @brief Built-in audio-DSP formula templates (the Formula Workbench `audio`
///        category — closes the intent of ROADMAP [3D_E-0022]).
///
/// Project Rule 6 designates the Formula Workbench as the home for numerical
/// design. Until now the Workbench had no `audio` category (its 14 categories
/// were physics, rendering, terrain, camera, water, wind, lighting, material,
/// color, denoise, pathtrace, sampling, post_processing, animation), so the
/// procedural-audio bundle's velocity->loudness/pitch and aggregate-event-rate
/// curves go through the fit/validate/export pipeline like every other domain.
///
/// These curves are consumed by the procedural-audio synthesis core
/// (engine/audio/procedural/, design doc phase_10_procedural_audio_design.md §9):
/// impact velocity (m/s) -> linear loudness gain + pitch multiplier, and foot/impact
/// speed -> PhISEM aggregate grain rate.
#pragma once

#include "formula/formula.h"

#include <vector>

namespace Vestige
{

/// @brief Factory for the built-in `audio` formula-template category.
///
/// Mirrors PhysicsTemplates: each `create*()` returns one FormulaDefinition with
/// `category = "audio"`; `createAll()` returns the full set, registered into the
/// FormulaLibrary by FormulaLibrary::registerBuiltinTemplates().
class AudioTemplates
{
public:
    /// @brief Creates all built-in audio formula templates.
    static std::vector<FormulaDefinition> createAll();

    // -- Individual template factories --------------------------------------

    /// @brief Impact approach speed (m/s) -> linear loudness gain [0,1].
    /// Saturating: quiet at a graze, approaching unity for a hard strike. The
    /// speed->gain normalisation lives in the fitted coefficient (no loose
    /// runtime `kRefSpeed`).
    static FormulaDefinition createImpactLoudnessGain();

    /// @brief Impact approach speed (m/s) -> pitch multiplier (~0.85..1.25).
    /// Harder strikes ring slightly brighter/higher.
    static FormulaDefinition createImpactPitchScale();

    /// @brief Aggregate footstep/impact speed (m/s) -> PhISEM grain rate (Hz).
    /// Faster contact spawns more micro-impact grains (sand/gravel/water).
    static FormulaDefinition createAggregateEventRate();
};

} // namespace Vestige

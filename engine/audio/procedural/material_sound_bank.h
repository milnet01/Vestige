// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file material_sound_bank.h
/// @brief Per-`SurfaceMaterial` procedural-sound bank (AX4 S5). One JSON file
///        (`assets/audio/synthesis/footstep_modal.json`) declares, per material,
///        which synthesis model (modal §5a / PhISEM §5b) plus its parameters.
///        A few KB of data replaces a WAV set per surface — the bundle's ~10×
///        asset saving.
///
/// `synthesize()` turns a (material, approach-speed, envelope-scale) request into
/// a 16-bit mono PCM one-shot: it applies the Formula Workbench velocity curves
/// (`audio_curves.h`) for loudness/pitch/grain-rate and the per-bank pitch/gain
/// jitter (§5c), then calls the pure §5a/§5b synth core. The RNG is injected (a
/// uniform in [0,1)) so the same emitter replays identically and tests are
/// byte-deterministic.
#pragma once

#include "audio/procedural/modal_synth.h"
#include "audio/procedural/phisem.h"
#include "physics/surface_material.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Vestige::Procedural
{

/// @brief Number of `SurfaceMaterial` enumerators (Default … Glass). Derived
///        from the enum so it tracks any append-only addition automatically.
inline constexpr std::size_t kSurfaceMaterialCount =
    static_cast<std::size_t>(SurfaceMaterial::Glass) + 1;

/// @brief Which synthesis model a material uses.
enum class SynthModel : std::uint8_t
{
    Modal,   ///< Struck solid — stone / wood / metal / glass (§5a).
    Phisem,  ///< Aggregate — sand / grass / water / cloth / dirt (§5b).
};

/// @brief One material's resolved bank entry. Modal fields are used when
///        `model == Modal`, PhISEM fields when `model == Phisem`.
struct MaterialSoundDef
{
    SynthModel model = SynthModel::Modal;

    // -- Modal (§5a) --
    std::vector<Mode> modes;            ///< Resonant modes (only first kMaxModes used).
    float pitchJitterCents = 0.0f;      ///< ± per-strike pitch spread (cents, §5c).
    float gainJitterDb     = 0.0f;      ///< ± per-strike gain spread (dB, §5c).

    // -- PhISEM (§5b) --
    float centreHz    = 1500.0f;        ///< Grain resonator centre frequency (Hz).
    float qual        = 2.0f;           ///< Resonator quality (higher = longer ring).
    float eventRateHz = 1000.0f;        ///< Grain rate at the nominal speed (kAggregateRefSpeedMps).
    float energyDecay = 18.0f;          ///< Per-second grain-energy decay over the event.

    // -- Shared --
    float durSec = 0.12f;               ///< Base duration (s); × envelopeScale, clamped to the synth cap.
};

/// @brief Loads + holds the per-material bank and synthesises strikes from it.
class MaterialSoundBank
{
public:
    /// @brief Constructs with the built-in fallback for every material
    ///        (a generic dull-thud modal entry), so `synthesize` is usable
    ///        before any JSON is loaded.
    MaterialSoundBank();

    /// @brief Parses a bank JSON file (schema: design §6). On success replaces
    ///        every authored material; materials absent from the file keep the
    ///        built-in fallback. Returns false (bank unchanged) on a missing /
    ///        oversized / malformed file. Unknown material names and unknown
    ///        keys are ignored.
    bool loadFromFile(const std::string& path);

    /// @brief Parses a bank from an in-memory JSON string (test seam / embedded
    ///        defaults). Same semantics as `loadFromFile`.
    bool loadFromString(const std::string& json);

    /// @brief The resolved entry for @a material (always valid — falls back to
    ///        `Default` then to the built-in thud).
    const MaterialSoundDef& defFor(SurfaceMaterial material) const;

    /// @brief Synthesises a one-shot strike for @a material into @a out.
    /// @param approachSpeed Contact speed (m/s); drives the FW loudness/pitch/
    ///        rate curves.
    /// @param envelopeScale 1.0 = footstep (bank `durSec`); >1 = longer impact
    ///        ring. Multiplies `durSec`, clamped to `kMaxDurationSec`.
    /// @param sample Injected uniform in [0,1) — pitch/gain jitter (modal) and
    ///        the Poisson grain process (PhISEM) both draw from it.
    /// @return Samples written to @a out (modal) or grain count (PhISEM); 0 on
    ///         a silent / empty strike.
    std::size_t synthesize(SurfaceMaterial material, float approachSpeed,
                           float envelopeScale, const std::function<float()>& sample,
                           std::vector<std::int16_t>& out) const;

private:
    std::array<MaterialSoundDef, kSurfaceMaterialCount> m_defs{};
};

// ---------------------------------------------------------------------------
// Variation helpers (§5c) — deterministic given an injected uniform.
// ---------------------------------------------------------------------------

/// @brief Multiplies @a base by a random pitch ratio within ±@a spreadCents
///        (1 semitone = 100 cents; ratio = 2^(cents/1200)). @a sample is a
///        uniform in [0,1); spread 0 returns @a base unchanged.
float jitterPitch(float base, float spreadCents, const std::function<float()>& sample);

/// @brief Multiplies @a base by a random gain within ±@a spreadDb
///        (ratio = 10^(dB/20)). @a sample is a uniform in [0,1); spread 0
///        returns @a base unchanged.
float jitterGain(float base, float spreadDb, const std::function<float()>& sample);

} // namespace Vestige::Procedural

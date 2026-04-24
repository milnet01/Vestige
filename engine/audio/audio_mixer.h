// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_mixer.h
/// @brief Phase 10 audio — mixer buses, ducking state machine, and
///        priority-based voice eviction.
///
/// Three independent primitives:
///
///   1. **Mixer buses** — a fixed set of named gain slots
///      (Master / Music / Voice / Sfx / Ambient / Ui) so settings
///      UI, user prefs, and per-source playback share a single
///      volume vocabulary. Each source belongs to exactly one
///      non-Master bus; `effectiveBusGain(mixer, bus)` returns the
///      product of that bus and Master for final playback gain.
///
///   2. **Ducking** — side-chain-style volume dip. While a trigger
///      signal is active (dialogue playing, important effect
///      firing) the ducking gain ramps from 1.0 down to
///      `duckFactor` with `attackSeconds`, and when the trigger
///      releases it ramps back up with `releaseSeconds`. The
///      state is pure per-channel: a mix bus and its ducking state
///      are both plain data, no OpenAL.
///
///   3. **Priority-based voice eviction** — when the source pool
///      is full the engine must pick which playing voice to drop
///      so a new one can start. Score each voice with priority
///      (hard tier) + effective gain (softer tiebreaker) + age
///      (oldest ties break last), then evict the lowest score.
///
/// All three are pure-function / data-only. The engine-side
/// AudioSystem applies the results to OpenAL gain / source
/// acquisition.
#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace Vestige
{

// ----- Priority --------------------------------------------------

/// @brief Importance tiers used for eviction scoring. Higher ranks
///        survive pool pressure; ties fall through to gain + age.
enum class SoundPriority
{
    Low,         ///< Background chatter, ambient one-shots — evict first.
    Normal,      ///< Standard SFX, footsteps, UI.
    High,        ///< Enemy attack cues, pickup confirms.
    Critical,    ///< Dialogue, boss stingers, objective audio.
};

/// @brief Stable label.
const char* soundPriorityLabel(SoundPriority p);

/// @brief Numeric rank (Low=0, Critical=3) for comparison math.
int soundPriorityRank(SoundPriority p);

// ----- Mixer buses ----------------------------------------------

/// @brief Fixed bus layout. Master is the final product; every
///        other bus is a sub-mix for its category.
enum class AudioBus
{
    Master,      ///< Global output gain — applied to everything.
    Music,       ///< Dynamic music layers.
    Voice,       ///< Dialogue, narration.
    Sfx,         ///< Gameplay sound effects.
    Ambient,     ///< Ambient beds, environmental one-shots.
    Ui,          ///< Menu clicks, confirmations, accessibility cues.
};

/// @brief Number of enum entries — sibling constant kept in sync.
constexpr std::size_t AudioBusCount = 6;

const char* audioBusLabel(AudioBus bus);

/// @brief Per-bus gain table. All gains default to 1.0 so a fresh
///        mixer plays at full authored level.
struct AudioMixer
{
    std::array<float, AudioBusCount> busGain;

    AudioMixer()
    {
        busGain.fill(1.0f);
    }

    /// @brief Set the stored gain for `bus`, clamped to [0, 1].
    ///
    /// Phase 10 slice 13.3: every non-test consumer should go
    /// through this setter rather than poking `busGain[i]` directly,
    /// so the clamp policy lives in exactly one place and the
    /// Settings apply path (applyAudio) does not silently admit
    /// out-of-range values written by a hand-edited settings.json.
    void setBusGain(AudioBus bus, float gain);

    /// @brief Raw stored gain for `bus` (no Master multiplication).
    ///        Use `effectiveBusGain(mixer, bus)` for the value that
    ///        actually scales outgoing sound.
    float getBusGain(AudioBus bus) const;
};

/// @brief Returns the gain that will be applied to a source routed
///        to `bus` — the product of that bus and the Master bus,
///        clamped to [0, 1].
float effectiveBusGain(const AudioMixer& mixer, AudioBus bus);

/// @brief Phase 10.7 slice A2 — composes the final OpenAL gain for
///        one playing source as `master × bus × sourceVolume`,
///        clamped to [0, 1]. Pure function so the gain math is
///        testable without an AL context.
///
/// @param mixer        Engine-owned bus-gain table.
/// @param bus          The source's assigned bus.
/// @param sourceVolume Per-source volume (`AudioSourceComponent::volume`
///                     or the `volume` argument to `playSound*`).
/// @returns Final gain in [0, 1]. The AudioEngine uploads this via
///          `alSourcef(id, AL_GAIN, ...)` at play time and every
///          frame thereafter (in `AudioEngine::updateGains`), so a
///          mid-play Settings slider move is audible on the next
///          frame.
float resolveSourceGain(const AudioMixer& mixer,
                        AudioBus bus,
                        float sourceVolume);

/// @brief Phase 10.9 P3 — 4-arg overload folding `DuckingState::currentGain`
///        into the composed gain so the Phase 10.7 ducking state machine
///        actually reaches `AL_GAIN`.
///
/// Final gain = `master × bus × sourceVolume × clamp01(duckingGain)`,
/// clamped to [0, 1]. `duckingGain` clamps to [0, 1] before the multiply
/// so a DuckingState that overshoots its floor (during attack / release
/// slew) can't push the composed gain outside unit range. Pass 1.0 when
/// ducking is not a concern — equivalent to the 3-arg overload.
float resolveSourceGain(const AudioMixer& mixer,
                        AudioBus bus,
                        float sourceVolume,
                        float duckingGain);

// ----- Ducking --------------------------------------------------

/// @brief Ducking parameters — attack pulls the gain down when the
///        trigger is active, release eases it back when released.
///        `duckFactor` is the floor the gain dips to while ducked.
struct DuckingParams
{
    float attackSeconds  = 0.08f;  ///< Downward slew duration (0→floor).
    float releaseSeconds = 0.30f;  ///< Upward slew duration (floor→1).
    float duckFactor     = 0.35f;  ///< Gain floor while ducked (0..1).
};

/// @brief Per-channel ducking state. `currentGain` is the live
///        multiplier the engine applies to the bus; it slews toward
///        either `duckFactor` (when `triggered`) or 1.0 (when
///        released) at the rate set by `params`.
struct DuckingState
{
    float currentGain = 1.0f;
    bool  triggered   = false;
};

/// @brief Advances `state.currentGain` by `deltaSeconds` using the
///        attack / release / floor values in `params`. Output is
///        clamped to [max(0, duckFactor), 1].
void updateDucking(DuckingState& state,
                    const DuckingParams& params,
                    float deltaSeconds);

// ----- Voice eviction -------------------------------------------

/// @brief Candidate voice for eviction scoring. `effectiveGain`
///        accounts for distance attenuation, occlusion, and bus
///        mix so a source playing silently drops before a louder
///        equal-priority neighbour. `ageSeconds` is how long the
///        voice has been playing — older ties evict first (newer
///        sounds are usually more important to the moment).
struct VoiceCandidate
{
    SoundPriority priority      = SoundPriority::Normal;
    float         effectiveGain = 1.0f;
    float         ageSeconds    = 0.0f;
};

/// @brief Composite score — higher = more worth keeping. Equal to
///        `priorityRank * 1000 + effectiveGain * 10 - ageSeconds`,
///        chosen so priority is the dominant axis and age is a
///        tiebreaker rather than a primary driver.
float voiceKeepScore(const VoiceCandidate& v);

/// @brief Returns the index of the voice most suitable to evict
///        (lowest keep-score), or `std::size_t{-1}` if the list is
///        empty. Caller releases that OpenAL source and starts the
///        new voice in its slot.
std::size_t chooseVoiceToEvict(const std::vector<VoiceCandidate>& voices);

/// @brief Phase 10.9 P7 — admission-controlled eviction: picks the
///        lowest keep-score voice iff its priority tier is strictly
///        lower than `incomingPriority`. Returns `std::size_t{-1}`
///        when no voice qualifies (incoming sound has equal or lower
///        priority than every existing voice and therefore drops).
///
/// Rationale: a Normal incoming sound should not be able to evict a
/// Normal-tier voice — equal-priority ties go to the incumbent, so a
/// rapid burst of same-priority sounds doesn't churn the pool. Only a
/// strictly higher incoming priority earns the right to kick someone
/// out. Within that rule the victim is still chosen by keep-score so
/// the quietest / oldest low-priority voice goes first.
///
/// The engine's pool-exhaustion retry in `AudioEngine::acquireSource`
/// calls this with the `SoundPriority` passed to the `playSound*`
/// overload; on a non-sentinel result it releases that source and
/// retries acquisition (guaranteed to succeed once a slot is freed).
std::size_t chooseVoiceToEvictForIncoming(
    const std::vector<VoiceCandidate>& voices,
    SoundPriority incomingPriority);

} // namespace Vestige

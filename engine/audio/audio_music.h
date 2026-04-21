// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_music.h
/// @brief Phase 10 audio — dynamic music system primitives.
///
/// Games rarely play a single music track. Instead, a small set of
/// parallel *layers* run continuously, each at a different gain:
/// ambient always-on at low volume, tension riding an enemy-
/// proximity signal, combat slamming in when the encounter starts,
/// discovery / danger / exploration covering narrative beats. The
/// player hears the sum.
///
/// This module ships three independent primitives:
///
///   1. **Layer state machine** — each `MusicLayerState` carries
///      `currentGain`, `targetGain`, and a `fadeSpeedPerSecond`
///      (gain units per second). `advanceMusicLayer(state, dt)`
///      slews `currentGain` toward `targetGain` without overshoot
///      so layer crossfades are smooth even when gameplay poke the
///      target every frame.
///
///   2. **Intensity → layer weights** — a single gameplay signal
///      in [0, 1] drives the relative mix. 0.0 = calm ambient-only;
///      1.0 = full combat. Interior values interpolate across
///      named layers so "exploration" dominates the middle third
///      rather than being a ghost between ambient and combat.
///
///   3. **Stinger queue** — short one-shots layered on top of the
///      continuous mix (stings for discoveries, jump-scare hits,
///      achievement flourishes). FIFO with push-newest /
///      drop-oldest when the queue is full so the most recent
///      events always win.
///
/// Silence is a first-class tool: the intensity-to-weights mapper
/// supports a `silence` fraction that subtracts from every layer,
/// so a scripted moment can cut all music without removing a layer
/// or stopping the tracks. The sum of layer weights can drop below
/// 1 during silence.
///
/// All three are pure data + pure-function logic — no OpenAL
/// linkage, no streaming IO. The engine-side MusicSystem drives
/// the layers via streaming voices on top of these primitives.
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace Vestige
{

// ----- Layers -----------------------------------------------------

/// @brief The six canonical music layers. Dense + stable for scene
///        persistence; add new entries at the end of the enum and
///        extend `MusicLayerCount` / `MusicLayerWeights` in lockstep.
enum class MusicLayer
{
    Ambient,        ///< Always on at low volume — base bed.
    Tension,        ///< Mid-intensity rise — approaching an encounter.
    Exploration,    ///< Mid-intensity calm — open area / discovery-prone.
    Combat,         ///< High-intensity — active fight.
    Discovery,      ///< Stinger-adjacent bed — used during scripted reveals.
    Danger,         ///< Highest-intensity — bosses / chase sequences.
};

/// @brief Number of layers — kept as a sibling constant so arrays
///        keyed by `MusicLayer` don't drift if the enum grows.
constexpr std::size_t MusicLayerCount = 6;

/// @brief Stable label — used by the mixer panel and debug logs.
const char* musicLayerLabel(MusicLayer layer);

/// @brief Per-layer slew state. `currentGain` is the live value the
///        engine-side voice listens to; `targetGain` is what the
///        gameplay logic writes; `fadeSpeedPerSecond` caps how
///        quickly `currentGain` moves toward `targetGain`.
struct MusicLayerState
{
    float currentGain        = 0.0f;
    float targetGain         = 0.0f;
    float fadeSpeedPerSecond = 0.5f;  ///< 0.5 = two seconds for a full 0→1 swing.
};

/// @brief Advances `currentGain` toward `targetGain` by at most
///        `fadeSpeedPerSecond * deltaSeconds`. `currentGain` is
///        clamped to [0, 1] on the way.
void advanceMusicLayer(MusicLayerState& state, float deltaSeconds);

// ----- Intensity → layer weights ---------------------------------

/// @brief One weight per canonical layer, addressable by both
///        `MusicLayer` (via `weightOf`) and array index. Weights are
///        in [0, 1]; during silence the sum may be < 1.
struct MusicLayerWeights
{
    std::array<float, MusicLayerCount> values{};

    float& weightOf(MusicLayer layer)
    {
        return values[static_cast<std::size_t>(layer)];
    }
    const float& weightOf(MusicLayer layer) const
    {
        return values[static_cast<std::size_t>(layer)];
    }
};

/// @brief Maps a single `intensity` signal in [0, 1] to the
///        relative mix across the six layers, with an optional
///        `silence` subtractor in [0, 1] that pulls every weight
///        toward zero for scripted quiet beats.
///
/// Intensity anchors (piece-wise linear across layers):
///   - 0.00 → Ambient 1, everything else 0.
///   - 0.25 → Ambient fades; Exploration peaks.
///   - 0.50 → Exploration fades; Tension peaks, Discovery starts.
///   - 0.75 → Tension fades; Combat peaks, Danger starts.
///   - 1.00 → Combat fades; Danger at 1.
///
/// Both `intensity` and `silence` are clamped to [0, 1]. The
/// resulting weights are each `layerWeight · (1 − silence)` — so
/// silence=1 collapses the whole mix to zero without touching the
/// underlying intensity routing.
MusicLayerWeights intensityToLayerWeights(float intensity, float silence = 0.0f);

// ----- Stingers --------------------------------------------------

/// @brief One-shot musical hit overlaid on top of the continuous
///        layers. Used for discoveries, jump scares, achievement
///        flourishes — sparse, event-driven.
struct MusicStinger
{
    std::string clipPath;
    float       delaySeconds = 0.0f;  ///< Countdown before playback starts.
    float       volume       = 1.0f;
};

/// @brief FIFO queue of pending stingers. Capacity is fixed so a
///        buggy caller spamming `enqueue` can't grow the queue
///        without bound.
class MusicStingerQueue
{
public:
    static constexpr std::size_t DEFAULT_CAPACITY = 8;

    MusicStingerQueue() = default;

    /// @brief Adjusts the maximum number of pending stingers. If
    ///        the queue currently holds more than `capacity`, the
    ///        oldest entries are dropped to fit.
    void setCapacity(std::size_t capacity);
    std::size_t capacity() const { return m_capacity; }

    /// @brief Pushes a stinger. If the queue is full the oldest
    ///        entry is evicted — the newest event always wins.
    void enqueue(const MusicStinger& stinger);

    /// @brief Advances all queued stingers by `deltaSeconds`.
    ///        Entries whose `delaySeconds` hits zero are popped
    ///        and returned in fire order (FIFO).
    std::vector<MusicStinger> advance(float deltaSeconds);

    /// @brief Discards every pending stinger without firing.
    void clear();

    std::size_t pending() const { return m_pending.size(); }

private:
    std::size_t m_capacity = DEFAULT_CAPACITY;
    std::vector<MusicStinger> m_pending;
};

} // namespace Vestige

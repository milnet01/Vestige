// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_mix_monitor.h
/// @brief Phase 10 AX12 — per-bus, producer-push analysis tap for the editor's
///        audio spectrum / waveform viewer.
///
/// CPU audio producers (the streaming music player, the procedural synth) push a
/// normalized-float mono copy of what they generate, tagged with bus + rate,
/// only while the Debug tab is active. The monitor sums a frame's *simultaneous*
/// submissions per bus and appends the result to that bus's rolling content ring;
/// the panel FFTs / draws the selected bus's ring. No OpenAL, no locking (single
/// main/update thread). See docs/phases/phase_10_audio_spectrum_viewer_design.md.
#pragma once

#include "audio/audio_mixer.h"  // AudioBus, AudioBusCount

#include <array>
#include <cstddef>
#include <vector>

namespace Vestige
{

/// @brief FFT block size (power of two) the spectrum transforms. ≈43 ms at 48 kHz.
constexpr std::size_t kMixMonitorWindow = 2048;
/// @brief Rolling waveform history retained per bus.
constexpr float kMixMonitorHistorySeconds = 2.0f;

/// @brief One producer's contribution to a bus for a frame: its gain-scaled
///        samples. Raw pointer — consumed synchronously by `accumulateBusFrame`.
struct MixSubmission
{
    const float* samples = nullptr;
    std::size_t  count   = 0;
    float        gain    = 1.0f;
};

/// @brief Pure combiner (no audio state): sum a bus's *simultaneous* submissions
///        into @a out. `out[i] = Σ gainⱼ · samplesⱼ[i]` for i < countⱼ (short
///        blocks zero-pad); `out.size()` == the max submitted count (0 if the
///        list is empty). Output is **not** clamped — over-unity sums survive so
///        the UI can flag clipping (INV-3). A single producer's *sequential*
///        chunks are concatenated by the producer before submit, never passed
///        here as separate submissions (INV-2).
void accumulateBusFrame(const std::vector<MixSubmission>& subs,
                        std::vector<float>& out);

/// @brief Per-bus producer-push monitor. Owned by `AudioEngine`; active only
///        while the editor Debug tab is shown.
class AudioMixMonitor
{
public:
    /// @brief Enable/disable capture. Disabling clears pending accumulators and
    ///        frees the content rings (the tab-closed zero-memory reclaim).
    void setActive(bool active);
    bool isActive() const { return m_active; }

    /// @brief Push a producer's frame block for @a bus (already normalized-float
    ///        mono, gain-scaled at the call site). No-op when inactive. Repeated
    ///        same-bus calls in a frame **sum** (simultaneous producers).
    void submit(AudioBus bus, const float* mono, std::size_t n,
                float gain, int sampleRate);

    /// @brief Append each bus's summed pending block to its content ring and
    ///        clear pending. Called once per frame by the panel, after all
    ///        producer systems have updated. A bus with no submit this frame
    ///        appends nothing (its display holds its last trace).
    void flushFrame();

    /// @brief The bus's rolling content ring (oldest → newest), up to capacity.
    const std::vector<float>& ring(AudioBus bus) const;
    /// @brief Most-recent submit rate for the bus (Hz), 0 if never submitted.
    int rateHz(AudioBus bus) const;
    /// @brief Did the bus receive a submit in the most recently flushed frame?
    bool hadRecentSignal(AudioBus bus) const;

private:
    struct BusState
    {
        std::vector<float> pending;          ///< this frame's summed submissions
        bool               pendingUsed = false;
        std::vector<float> content;          ///< rolling ring, trimmed to capacity
        int                rate = 0;
        bool               hadRecentSignal = false;
    };

    std::size_t index(AudioBus bus) const { return static_cast<std::size_t>(bus); }

    bool                                    m_active = false;
    std::array<BusState, AudioBusCount>     m_bus;
};

} // namespace Vestige

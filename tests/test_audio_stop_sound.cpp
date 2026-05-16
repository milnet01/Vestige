// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_stop_sound.cpp
/// @brief Phase 10.9 Slice 15 Au1 — `stopSound(handle)` API contract.
///
/// The audit's headline finding: "32 looping calls currently freeze the
/// mixer with no caller-side stop path." The `playSound*` family already
/// returned the OpenAL source ID since Phase 10.9 P7, but no public
/// `stopSound`-named entry point existed. Au1 adds that public surface
/// so the contract reads naturally — `playSound` paired with `stopSound`
/// — and updates the docstring to spell out the looping-caller's
/// responsibility (hold the handle, call stopSound to terminate).

#include <gtest/gtest.h>

#include "audio/audio_engine.h"

#include <glm/glm.hpp>
#include <limits>

using namespace Vestige;

TEST(AudioEngineStopSound, ZeroHandleIsSafeNoOp_Au1)
{
    // Calling stopSound with the failure return from playSound* (0)
    // must be a no-op so callers don't have to branch on the return.
    AudioEngine engine;  // not initialised — m_available stays false
    EXPECT_NO_THROW(engine.stopSound(0));
}

TEST(AudioEngineStopSound, StaleHandleIsSafeNoOp_Au1)
{
    // Passing a handle that does not belong to this engine's pool
    // (different engine / already-released / fabricated) must not
    // crash. `releaseSource` walks the pool and silently drops misses.
    AudioEngine engine;
    EXPECT_NO_THROW(engine.stopSound(123456));
}

TEST(AudioEngineStopSound, StopSoundIsAliasForReleaseSource_Au1)
{
    // Headless invariant: the new public API forwards to releaseSource,
    // so calling either with the same handle has the same effect.
    // Without an audio device both are no-ops, but the existence of the
    // new API surface is what Au1 closes.
    AudioEngine engine;
    EXPECT_NO_THROW(engine.releaseSource(0));
    EXPECT_NO_THROW(engine.stopSound(0));
}

TEST(AudioEngineStopSound, PlaySoundReturnsHandleEvenWhenUnavailable_Au1)
{
    // Pre-Au1 P7 already promised this but the API contract is
    // re-pinned alongside Au1: every playSound* overload returns the
    // source ID (or 0 on failure), never void. A looping caller can
    // therefore always grab the handle and call stopSound when done.
    AudioEngine engine;  // unavailable → returns 0
    const unsigned int handle =
        engine.playSound("any.wav", glm::vec3(0.0f), 1.0f, /*loop=*/true);
    EXPECT_EQ(handle, 0u) << "without audio hardware playSound returns 0";
}

// =============================================================================
// Phase 10.9 Slice 18 Ts2 — AudioEngine API surface coverage.
//
// These tests pin the no-device safety contract for every public
// engine method that the cold-eyes audit found untested. The
// underlying contract is the same for each: a freshly-constructed
// AudioEngine (no `initialize()` call) must accept every setter and
// getter without crashing — the audio system gets driven from many
// places (panel, scripting, settings) and a no-device runtime is
// common (CI, headless tools).
// =============================================================================

TEST(AudioEngineApiSurface, StopAllIsSafeOnUnavailableEngine_Ts2)
{
    AudioEngine engine;
    EXPECT_NO_THROW(engine.stopAll());
    // Idempotent — calling twice without intervening playSound must
    // not blow up.
    EXPECT_NO_THROW(engine.stopAll());
}

TEST(AudioEngineApiSurface, SetListenerVelocityIsSafeOnUnavailableEngine_Ts2)
{
    AudioEngine engine;
    EXPECT_NO_THROW(engine.setListenerVelocity(glm::vec3(0.0f)));
    EXPECT_NO_THROW(engine.setListenerVelocity(glm::vec3(10.0f, 0.0f, -3.5f)));
    // Extreme values: any callsite-injected NaN must not propagate to
    // a crash inside the engine — clamping/rejection happens in the
    // pure-math layer (see test_audio_doppler.cpp). Worst case here
    // is the engine silently stores NaN; no exception or signal.
    EXPECT_NO_THROW(engine.setListenerVelocity(
        glm::vec3(std::numeric_limits<float>::quiet_NaN())));
}

TEST(AudioEngineApiSurface, DistanceModelSettersAreSafeOnUnavailableEngine_Ts2)
{
    AudioEngine engine;
    EXPECT_NO_THROW(engine.setDistanceModel(AttenuationModel::Linear));
    EXPECT_NO_THROW(engine.setDistanceModel(AttenuationModel::InverseDistance));
    EXPECT_NO_THROW(engine.setDopplerFactor(0.0f));
    EXPECT_NO_THROW(engine.setDopplerFactor(1.0f));
    EXPECT_NO_THROW(engine.setDopplerFactor(-1.0f));    // clamp expected
    EXPECT_NO_THROW(engine.setSpeedOfSound(343.3f));
    EXPECT_NO_THROW(engine.setSpeedOfSound(0.0f));      // clamp expected
    EXPECT_NO_THROW(engine.setSpeedOfSound(-100.0f));   // clamp expected
}

TEST(AudioEngineApiSurface, GetAvailableHrtfDatasetsReturnsEmptyOnUnavailable_Ts2)
{
    AudioEngine engine;
    // Without an audio device the driver can't enumerate HRTFs —
    // the engine must return an empty vector, not crash or read
    // uninitialised memory.
    const std::vector<std::string> datasets = engine.getAvailableHrtfDatasets();
    EXPECT_TRUE(datasets.empty());
}

TEST(AudioEngineApiSurface, AcquireSourceReturnsZeroOnUnavailable_Ts2)
{
    AudioEngine engine;
    // The priority-based eviction path is pinned by `test_audio_mixer.cpp`'s
    // pure-function tests; the engine-side wrapper must short-circuit
    // to 0 (no source allocated) when there's no audio device.
    EXPECT_EQ(engine.acquireSource(SoundPriority::High), 0u);
}

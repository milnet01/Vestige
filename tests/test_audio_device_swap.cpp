// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_device_swap.cpp
/// @brief Phase 10 audio quick-wins (AX11) — coverage for audio-device
///        hot-swap. The pure policy decision table + the settings-token
///        round-trip live in the headless `audio_device_hotswap` module;
///        the poll state machine, the {Off,Notify,Auto} dispatch, the
///        callback→main name hand-off contract, and the post-swap HRTF
///        re-evaluation are exercised on a default-constructed (never
///        initialised) `AudioEngine` — the actual `alcReopenDeviceSOFT`
///        is gated behind `isAvailable()`, so no audio device is needed.

#include <gtest/gtest.h>

#include "audio/audio_device_hotswap.h"
#include "audio/audio_engine.h"

#include <string>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Pure policy decision table (headless module)
// ---------------------------------------------------------------------------

TEST(AudioDeviceHotSwap, DecisionTableMapsEveryMode)
{
    EXPECT_EQ(decideDeviceSwapAction(DeviceHotSwapMode::Off),
              DeviceSwapAction::Ignore);
    EXPECT_EQ(decideDeviceSwapAction(DeviceHotSwapMode::Notify),
              DeviceSwapAction::Notify);
    EXPECT_EQ(decideDeviceSwapAction(DeviceHotSwapMode::Auto),
              DeviceSwapAction::Swap);
}

TEST(AudioDeviceHotSwap, ModeStringRoundTrips)
{
    for (auto mode : {DeviceHotSwapMode::Off, DeviceHotSwapMode::Notify,
                      DeviceHotSwapMode::Auto})
    {
        EXPECT_EQ(deviceHotSwapModeFromString(deviceHotSwapModeToString(mode)),
                  mode);
    }
}

TEST(AudioDeviceHotSwap, UnknownTokenFallsBackToProvidedDefault)
{
    EXPECT_EQ(deviceHotSwapModeFromString("nonsense", DeviceHotSwapMode::Auto),
              DeviceHotSwapMode::Auto);
    // Default fallback is Notify (the safe mid-session policy).
    EXPECT_EQ(deviceHotSwapModeFromString(""), DeviceHotSwapMode::Notify);
}

// ---------------------------------------------------------------------------
// Poll state machine + callback→main hand-off contract
// ---------------------------------------------------------------------------

TEST(AudioDeviceHotSwap, ChangeIsPendingUntilPolledThenCleared)
{
    AudioEngine engine;  // never initialised — no audio device
    engine.setDeviceHotSwapMode(DeviceHotSwapMode::Off);

    EXPECT_FALSE(engine.isDeviceChangePending());

    // Simulate the OpenAL event thread flagging a default-device change.
    engine.onDeviceChanged("USB Headphones");
    EXPECT_TRUE(engine.isDeviceChangePending());

    // The poll consumes the pending change exactly once.
    EXPECT_FALSE(engine.pollAndHandleDeviceChange());  // Off ⟹ no swap
    EXPECT_FALSE(engine.isDeviceChangePending());

    // A second poll with nothing pending is a no-op.
    EXPECT_FALSE(engine.pollAndHandleDeviceChange());
}

TEST(AudioDeviceHotSwap, IdlePollIsANoOp)
{
    AudioEngine engine;
    EXPECT_FALSE(engine.pollAndHandleDeviceChange());
    EXPECT_FALSE(engine.isDeviceChangePending());
}

// ---------------------------------------------------------------------------
// {Off, Notify, Auto} dispatch
// ---------------------------------------------------------------------------

TEST(AudioDeviceHotSwap, OffModeNeverNotifies)
{
    AudioEngine engine;
    engine.setDeviceHotSwapMode(DeviceHotSwapMode::Off);

    bool notified = false;
    engine.setDeviceChangeListener([&](const std::string&) { notified = true; });

    engine.onDeviceChanged("Speakers");
    EXPECT_FALSE(engine.pollAndHandleDeviceChange());
    EXPECT_FALSE(notified);
}

TEST(AudioDeviceHotSwap, NotifyModeFiresListenerWithStashedNameAndDefersSwap)
{
    AudioEngine engine;
    engine.setDeviceHotSwapMode(DeviceHotSwapMode::Notify);

    std::string heardName;
    int         calls = 0;
    engine.setDeviceChangeListener([&](const std::string& name)
                                   { heardName = name; ++calls; });

    engine.onDeviceChanged("USB Headphones");

    // Notify dispatches the listener but does NOT report a swap (the swap is
    // deferred to the UI confirm path).
    EXPECT_FALSE(engine.pollAndHandleDeviceChange());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(heardName, "USB Headphones");  // exact name handed off
}

TEST(AudioDeviceHotSwap, AutoModeRequestsHrtfReevalAfterSwap)
{
    AudioEngine engine;  // unavailable ⟹ the device reopen no-ops...
    engine.setDeviceHotSwapMode(DeviceHotSwapMode::Auto);

    // ...but the post-swap HRTF re-evaluation fires the status listener
    // unconditionally, which is the observable "re-eval requested" signal.
    int hrtfReevals = 0;
    engine.setHrtfStatusListener([&](const HrtfStatusEvent&) { ++hrtfReevals; });

    engine.onDeviceChanged("HDMI Output");
    // No live device ⟹ no actual swap occurred, so poll reports false.
    EXPECT_FALSE(engine.pollAndHandleDeviceChange());
    EXPECT_EQ(hrtfReevals, 1);
}

TEST(AudioDeviceHotSwap, ConfirmPathReevalsHrtfEvenWithoutDevice)
{
    AudioEngine engine;
    int hrtfReevals = 0;
    engine.setHrtfStatusListener([&](const HrtfStatusEvent&) { ++hrtfReevals; });

    // The Notify confirm path calls reopenToDefaultDevice directly; it
    // no-ops the reopen (no device) but still re-evaluates HRTF.
    EXPECT_FALSE(engine.reopenToDefaultDevice("Bluetooth Speaker"));
    EXPECT_EQ(hrtfReevals, 1);
}

TEST(AudioDeviceHotSwap, ModeAndDefaultAccessors)
{
    AudioEngine engine;
    // Default policy is Notify.
    EXPECT_EQ(engine.getDeviceHotSwapMode(), DeviceHotSwapMode::Notify);
    engine.setDeviceHotSwapMode(DeviceHotSwapMode::Auto);
    EXPECT_EQ(engine.getDeviceHotSwapMode(), DeviceHotSwapMode::Auto);
}

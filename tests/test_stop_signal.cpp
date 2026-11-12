// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_stop_signal.cpp
/// @brief Ctrl-C and SIGTERM set a flag instead of ending the process
///        (3D_E-0969).
#include "core/stop_signal.h"

#include <gtest/gtest.h>

#include <csignal>

using namespace Vestige;

namespace
{

/// Puts the default actions back, so a later test's signal behaves as usual.
void restoreDefaults()
{
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
#ifdef SIGBREAK
    std::signal(SIGBREAK, SIG_DFL);
#endif
}

} // namespace

TEST(StopSignal, NothingReceivedAfterInstalling)
{
    installStopSignalHandlers();
    EXPECT_FALSE(stopSignalReceived());
    restoreDefaults();
}

// Without the handler, raising either signal would end the test process.
TEST(StopSignal, InterruptAndTerminateSetTheFlagAndTheProcessLives)
{
    for (int signalNumber : {SIGINT, SIGTERM})
    {
        installStopSignalHandlers();
        ASSERT_FALSE(stopSignalReceived());
        std::raise(signalNumber);
        EXPECT_TRUE(stopSignalReceived()) << "signal " << signalNumber;
    }
    restoreDefaults();
}

#ifdef SIGBREAK
TEST(StopSignal, CtrlBreakSetsTheFlag)
{
    installStopSignalHandlers();
    std::raise(SIGBREAK);
    EXPECT_TRUE(stopSignalReceived());
    restoreDefaults();
}
#endif

// A second signal must be able to end a stuck engine: the first one puts the
// default action back.
TEST(StopSignal, TheFirstSignalRestoresTheDefaultAction)
{
    installStopSignalHandlers();
    std::raise(SIGINT);
    const auto previous = std::signal(SIGINT, SIG_DFL);
    EXPECT_EQ(previous, SIG_DFL);
    restoreDefaults();
}

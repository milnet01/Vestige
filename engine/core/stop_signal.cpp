// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file stop_signal.cpp
/// @brief Stop-signal handlers.
#include "core/stop_signal.h"

#include <csignal>

namespace Vestige
{

namespace
{

// The only type a signal handler may write.
volatile std::sig_atomic_t g_stopSignalReceived = 0;

extern "C" void onStopSignal(int signalNumber)
{
    g_stopSignalReceived = 1;
    // Back to the default action: a second signal ends the process.
    std::signal(signalNumber, SIG_DFL);
}

} // namespace

void installStopSignalHandlers()
{
    g_stopSignalReceived = 0;
    std::signal(SIGINT, onStopSignal);
    std::signal(SIGTERM, onStopSignal);
#ifdef SIGBREAK
    // Windows: Ctrl-Break, which is what a parent process can send to a
    // console process group (frameledger's `alternate` stops a run with it).
    std::signal(SIGBREAK, onStopSignal);
#endif
}

bool stopSignalReceived()
{
    return g_stopSignalReceived != 0;
}

} // namespace Vestige

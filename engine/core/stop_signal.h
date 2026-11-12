// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file stop_signal.h
/// @brief Ctrl-C, SIGTERM and Windows' Ctrl-Break ask the engine to stop
///        (3D_E-0969).
///
/// Without a handler those end the process on the spot: no shutdown, and a
/// frame log with no close line, which reads as a crashed run. The handler
/// only sets a flag; the main loop reads it and leaves the normal way.
#pragma once

namespace Vestige
{

/// @brief Installs the handlers. A second signal of the same kind ends the
///        process at once, so an engine that is stuck can still be stopped.
void installStopSignalHandlers();

/// @brief Whether a stop signal has arrived since the handlers were installed.
bool stopSignalReceived();

} // namespace Vestige

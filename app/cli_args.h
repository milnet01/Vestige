// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cli_args.h
/// @brief CLI argument parsing for the Vestige application, extracted from
///        main.cpp so the flag-to-EngineConfig mapping can be unit-tested
///        headlessly (no GL context / no engine bring-up).
#pragma once

#include "core/engine.h"

namespace Vestige
{

/// @brief Prints the CLI usage/help text to stdout.
void printUsage(const char* argv0);

/// @brief Parses @a argv into @a config.
/// @param exitCode Set to the process exit code when the function returns false
///        (0 for `--help`, 2 for a parse error / missing argument value).
/// @return true when parsing succeeded and main should continue; false when
///         main should return @a exitCode immediately.
bool parseArgs(int argc, char* argv[], EngineConfig& config, int& exitCode);

} // namespace Vestige

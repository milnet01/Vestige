// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file posix_env_shim.h
/// @brief Portable setenv/unsetenv for tests. POSIX provides these; MSVC does
///        not, so map them onto _putenv_s. Include this in any test that mutates
///        the environment so the test suite builds on Windows.
#pragma once

#ifdef _WIN32
#include <cstdlib>

// Signature-compatible with POSIX setenv (the `overwrite` flag is ignored —
// _putenv_s always overwrites, which matches how the tests use it).
inline int setenv(const char* name, const char* value, int /*overwrite*/)
{
    return ::_putenv_s(name, value);
}

// POSIX unsetenv: remove the variable. _putenv_s with an empty value removes it.
inline int unsetenv(const char* name)
{
    return ::_putenv_s(name, "");
}
#else
#include <cstdlib>  // setenv / unsetenv
#endif

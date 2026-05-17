// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_helpers.h
/// @brief Shared scaffolding for the test binary — replaces 15× copy-pasted
///        `VESTIGE_GETPID` macros and the per-test temp-dir stamping idiom.
///
/// Surfaced by `/test-audit` on 2026-05-16: 13 of the 15 PID-stamping
/// fixtures lacked the `current_test_info()->name()` suffix, so they
/// were one fixed-filename test away from a `ctest -j` collision. This
/// header collapses both pieces — the PID macro and the per-test stamp
/// — to a single point of truth.
#pragma once

#include "core/logger.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#ifdef _WIN32
#include <process.h>
#define VESTIGE_GETPID() _getpid()
#else
#include <unistd.h>
#define VESTIGE_GETPID() getpid()
#endif

namespace Vestige
{
namespace Testing
{

/// @brief Returns a `<pid>_<test-name>` string unique within a single
///        ctest run, so two fixtures (or two TEST_F cases under the same
///        fixture, even with `ctest -j`) never share a temp directory.
///
/// Use as the per-test suffix when constructing a sandbox path:
/// `fs::temp_directory_path() / ("vestige_foo_" + vestigeTestStamp())`.
inline std::string vestigeTestStamp()
{
    const auto* info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    std::string stamp = std::to_string(VESTIGE_GETPID());
    if (info)
    {
        stamp += "_";
        stamp += info->name();
    }
    return stamp;
}

/// @brief Counts ring-buffer entries at @c LogLevel::Warning whose
///        message contains @a needle.
///
/// Surfaced by `/test-audit` 2026-05-17: an earlier copy in
/// `test_input_bindings.cpp` had dropped the level filter, so the
/// matching `EXPECT_GE` checks would pass on Info/Error lines that
/// happened to contain the needle. Centralising the helper here forces
/// every call-site through the level-filtered path.
inline std::size_t countWarningsContaining(const std::string& needle)
{
    std::size_t count = 0;
    for (const auto& entry : Logger::getEntries())
    {
        if (entry.level == LogLevel::Warning
            && entry.message.find(needle) != std::string::npos)
        {
            ++count;
        }
    }
    return count;
}

} // namespace Testing
} // namespace Vestige

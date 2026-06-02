// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_result.cpp
/// @brief Phase 10.9 CE3 — pin the `Vestige::Result<T, E>` vocabulary type.
///
/// `engine/utils/result.h` aliases `Result` to `std::expected` on a C++23
/// toolchain and to the vendored `tl::expected` under the C++17 baseline
/// (CODING_STANDARDS.md §11). These tests pin the shared public surface both
/// backends must expose, so a future C++23 standard bump (which flips the
/// alias) is caught here if the two backends ever diverge in behaviour. The
/// `static_assert` below also documents which backend the current build
/// selected.

#include <gtest/gtest.h>

#include "utils/result.h"

#include <string>
#include <type_traits>

using Vestige::makeUnexpected;
using Vestige::Result;
using Vestige::Unexpected;

namespace
{

enum class IoError
{
    NotFound,
    Denied,
};

Result<int, IoError> parseInt(bool ok)
{
    if (ok)
    {
        return 42;
    }
    return makeUnexpected(IoError::NotFound);
}

Result<void, IoError> commit(bool ok)
{
    if (ok)
    {
        return {};
    }
    return makeUnexpected(IoError::Denied);
}

} // namespace

// The alias must resolve to whichever backend the build selected — this both
// documents the selection and fails loudly if result.h's detection breaks.
#if defined(VESTIGE_RESULT_USE_STD_EXPECTED)
static_assert(std::is_same_v<Result<int, IoError>, std::expected<int, IoError>>,
              "C++23 build must alias Result to std::expected");
#else
static_assert(std::is_same_v<Result<int, IoError>, tl::expected<int, IoError>>,
              "C++17 build must alias Result to tl::expected");
#endif

TEST(ResultCE3, HoldsValueOnSuccess)
{
    Result<int, IoError> r = parseInt(true);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r.value(), 42);
    EXPECT_EQ(*r, 42);
}

TEST(ResultCE3, HoldsErrorOnFailure)
{
    Result<int, IoError> r = parseInt(false);
    ASSERT_FALSE(r.has_value());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error(), IoError::NotFound);
}

TEST(ResultCE3, ValueOrReturnsFallbackOnError)
{
    EXPECT_EQ(parseInt(false).value_or(7), 7);
    EXPECT_EQ(parseInt(true).value_or(7), 42);
}

TEST(ResultCE3, VoidSpecialisationSuccess)
{
    Result<void, IoError> r = commit(true);
    EXPECT_TRUE(r.has_value());
}

TEST(ResultCE3, VoidSpecialisationError)
{
    Result<void, IoError> r = commit(false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), IoError::Denied);
}

TEST(ResultCE3, UnexpectedAliasConstructsError)
{
    Result<int, IoError> r = Unexpected<IoError>(IoError::Denied);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), IoError::Denied);
}

TEST(ResultCE3, MonadicAndThenChainsOnSuccess)
{
    Result<int, IoError> r = parseInt(true).and_then(
        [](int v) -> Result<int, IoError> { return v * 2; });
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 84);
}

TEST(ResultCE3, MonadicAndThenShortCircuitsOnError)
{
    bool called = false;
    Result<int, IoError> r = parseInt(false).and_then(
        [&](int v) -> Result<int, IoError>
        {
            called = true;
            return v * 2;
        });
    EXPECT_FALSE(called);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), IoError::NotFound);
}

TEST(ResultCE3, MonadicTransformMapsValue)
{
    Result<int, IoError> r =
        parseInt(true).transform([](int v) { return v + 1; });
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 43);
}

TEST(ResultCE3, MonadicOrElseRecoversFromError)
{
    Result<int, IoError> r = parseInt(false).or_else(
        [](IoError) -> Result<int, IoError> { return 0; });
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 0);
}

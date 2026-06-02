// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file result.h
/// @brief Engine-wide `Result<T, E>` value type for errors-as-values.
///
/// `Result<T, E>` is the engine's name for "an operation that yields a `T`
/// on success or an `E` on failure" (CODING_STANDARDS.md §11). It is a thin
/// alias over `std::expected<T, E>` (C++23) where the toolchain provides a
/// working `<expected>`, and over the vendored `tl::expected` reference
/// implementation otherwise. Both backends share the same public surface
/// (`has_value`, `value`, `error`, `value_or`, the monadic `and_then` /
/// `or_else` / `transform`, and the `void` partial specialisation), so
/// callers write identical code regardless of which backend is selected.
///
/// Why a fallback exists: the engine compiles at the C++17 baseline
/// (`CMAKE_CXX_STANDARD 17`), and `std::expected` is a C++23 *library*
/// feature — `<expected>` is unreachable under `-std=c++17` ("only available
/// from C++23 onwards"). `tl::expected` is the implementation `std::expected`
/// was standardised from, so the fallback matches the standard's semantics.
/// When the project later moves to C++23 the detection below switches to the
/// standard type with no caller changes. See THIRD_PARTY_NOTICES.md for the
/// vendored dependency.
///
/// Backend detection (CODING_STANDARDS.md §11):
///   - A C++23 language mode (`__cplusplus >= 202302L`) plus a present
///     `<expected>` header is the precondition. `<version>` is pulled in
///     first so the `__cpp_lib_expected` / `_LIBCPP_VERSION` macros the
///     branches below read are actually defined (a feature-test macro is
///     undefined until its declaring header is included).
///   - libc++ shipped a *broken* `__cpp_lib_expected` on Clang 16 with
///     missing monadic ops until Clang 18 (LLVM #108011), so for libc++ we
///     gate on Clang 18+ rather than trusting the feature-test macro.
///   - libstdc++ and the MSVC STL report `__cpp_lib_expected` reliably.
/// On any build that fails these checks (today: every build, since the
/// baseline is C++17) the `tl::expected` fallback is used.
#pragma once

#if __cplusplus >= 202302L && defined(__has_include)
#  if __has_include(<version>)
#    include <version> // defines __cpp_lib_expected / _LIBCPP_VERSION
#  endif
#  if __has_include(<expected>)
#    if defined(_LIBCPP_VERSION)
//       libc++: trust only Clang 18+ (the feature-test macro is unreliable on
//       Clang 16's libc++ — see file header / LLVM #108011).
#      if defined(__clang_major__) && __clang_major__ >= 18
#        define VESTIGE_RESULT_USE_STD_EXPECTED 1
#      endif
#    elif defined(__cpp_lib_expected) && __cpp_lib_expected >= 202211L
//       libstdc++ / MSVC STL: feature-test macro is reliable here.
#      define VESTIGE_RESULT_USE_STD_EXPECTED 1
#    endif
#  endif
#endif

#if defined(VESTIGE_RESULT_USE_STD_EXPECTED)
#  include <expected>
#else
#  include <tl/expected.hpp>
#endif

#include <utility> // std::forward, std::decay_t

namespace Vestige
{

#if defined(VESTIGE_RESULT_USE_STD_EXPECTED)

/// @brief Success-or-error return type. `Result<void, E>` is valid and uses
///        the standard `void` specialisation (a value-less success state).
template <class T, class E>
using Result = std::expected<T, E>;

/// @brief Error wrapper used to return the failure value of a `Result`.
template <class E>
using Unexpected = std::unexpected<E>;

#else

template <class T, class E>
using Result = tl::expected<T, E>;

template <class E>
using Unexpected = tl::unexpected<E>;

#endif

/// @brief Constructs an `Unexpected<E>` from an error value, deducing `E`.
///
/// Smooths over the lack of alias-template CTAD under C++17 (where
/// `Unexpected{err}` cannot deduce `E`) and over the fact that `std::expected`
/// — unlike `tl::expected` — provides no `make_unexpected` free function.
/// Usage: `return Vestige::makeUnexpected(IoError::NotFound);`
template <class E>
[[nodiscard]] constexpr auto makeUnexpected(E&& error)
{
    return Unexpected<std::decay_t<E>>(std::forward<E>(error));
}

} // namespace Vestige

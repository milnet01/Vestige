// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file lsan_guard.h
/// @brief RAII that excludes intentional third-party process-lifetime
///        allocations from LeakSanitizer's report.
///
/// Some libraries we link in tests allocate state whose lifetime equals the
/// process and which they never free (the OS reclaims it at exit): GLFW's
/// platform init / teardown, the Mesa driver, and — under the llvmpipe software
/// rasterizer used in CI — JIT-compiled shader pipe-state allocated on the first
/// `glDrawArrays`. These are not Vestige leaks and there is no code that frees
/// them. Bracketing the exact third-party call with this guard is more precise
/// than a symbol/module suppression file (and works even when the fast unwinder
/// can't symbolize the driver DSO, where a `leak:<symbol>` entry silently
/// misses and the leak surfaces as `<unknown module>`).
///
/// Use ONLY around third-party calls. Test-body allocations keep full tracking.
#pragma once

#if defined(__has_feature)
#  if __has_feature(address_sanitizer) || __has_feature(leak_sanitizer)
#    define VESTIGE_LSAN_AVAILABLE 1
#  endif
#endif
#if !defined(VESTIGE_LSAN_AVAILABLE) && defined(__SANITIZE_ADDRESS__)
#  define VESTIGE_LSAN_AVAILABLE 1
#endif
#if defined(VESTIGE_LSAN_AVAILABLE)
#  include <sanitizer/lsan_interface.h>
#endif

namespace Vestige::Test
{

/// While alive, LeakSanitizer does not track heap allocations on the current
/// thread. No-op when not built with a sanitizer.
struct ScopedLeakCheckDisable
{
#if defined(VESTIGE_LSAN_AVAILABLE)
    ScopedLeakCheckDisable() { __lsan_disable(); }
    ~ScopedLeakCheckDisable() { __lsan_enable(); }
#else
    ScopedLeakCheckDisable() = default;
#endif
    ScopedLeakCheckDisable(const ScopedLeakCheckDisable&) = delete;
    ScopedLeakCheckDisable& operator=(const ScopedLeakCheckDisable&) = delete;
};

}  // namespace Vestige::Test

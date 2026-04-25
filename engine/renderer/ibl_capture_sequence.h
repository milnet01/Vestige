// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ibl_capture_sequence.h
/// @brief Wraps an IBL capture pass sequence in a forward-Z scope so cubemap
/// face captures don't corrupt the engine's reverse-Z global GL state.
///
/// `EnvironmentMap::generate` and `LightProbe::generateFromCubemap` each run
/// a sequence of cubemap-face render passes (capture, irradiance convolution,
/// GGX prefilter, BRDF LUT). Those passes need standard NDC depth
/// (`GL_NEGATIVE_ONE_TO_ONE` + `GL_LESS` + clearDepth 1.0); the engine's
/// scene draw runs reverse-Z (`GL_ZERO_TO_ONE` + `GL_GEQUAL` + clearDepth
/// 0.0). Without an outer guard, the IBL passes either render against a
/// reverse-Z depth buffer (corrupting the captured cubemap) or, on the
/// init-time first-generation path at `renderer.cpp:683-692`, leave the
/// engine in forward-Z afterward (corrupting every subsequent scene draw).
///
/// This header lifts the bracket pattern into a single helper so both
/// EnvironmentMap and LightProbe call the same code, and so the bracket
/// contract can be unit-tested without a GL context (production uses
/// `ScopedForwardZ`; tests inject a recording mock guard).
#pragma once

#include <functional>
#include <initializer_list>

namespace Vestige
{

/// @brief Runs `steps` in order, wrapped in `Guard`.
///
/// Production callers use `runIblCaptureSequence` (below) which fixes
/// `Guard = ScopedForwardZ`. Tests pass a recording mock guard to verify
/// the bracket contract without a GL context.
///
/// Null `std::function<void()>` entries are skipped. The guard's
/// destructor fires after the last step has returned, including the
/// case where `steps` is empty.
template <typename Guard, typename StepsList>
void runIblCaptureSequenceWith(const StepsList& steps)
{
    Guard guard;
    for (const auto& step : steps)
    {
        if (step) step();
    }
}

/// @brief Production overload: runs `steps` wrapped in `ScopedForwardZ`.
///
/// Defined out-of-line so the header has no `glad` dependency for
/// headless callers (e.g. tests using `runIblCaptureSequenceWith`).
void runIblCaptureSequence(std::initializer_list<std::function<void()>> steps);

} // namespace Vestige

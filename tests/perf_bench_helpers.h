// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file perf_bench_helpers.h
/// @brief Shared timing harness for wall-clock perf gates (3D_E-0626,
///        3D_E-0656): time-bounded warm-up, many timed runs, gate on the
///        minimum, report min / median / max.
#pragma once

#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

namespace Vestige::Test
{

// One timing scheme for every wall-clock perf gate (3D_E-0626, shared since
// 3D_E-0656). Written for the fog GPU gates in test_fog_benchmark.cpp; the
// CPU gates use it too, because the defect it fixes is not GPU-specific: a
// median of a few frames taken right after start-up samples the transient
// and any preemption, and a minimum of many does not.
//
// What it replaces: three warm-up frames, then the median of eight. That
// sampled inside the start-up transient. Five runs of the god-ray pass on the
// RX 6600, same binary and same preset, returned 549.8 to 1199.9 µs -- a 76%
// spread, against budgets policed to a few per cent.
//
// The transient has a different shape on each machine in the estate, so the
// warm-up is bounded by TIME rather than by frames. The RX 6600 settles within
// about fifteen frames; the GTX 1050 ramps its clocks for roughly half a second
// (measured 2026-09-02: uncontended cost 1684 µs in the first 100 ms, 1481 µs at
// 400-500 ms, flat at ~1460 µs thereafter). A frame count that covers the
// second is wasteful on the first, and one that suits the first does not reach
// steady clocks on the second.
//
// The gated statistic is the MINIMUM, not a median and not a high percentile.
// Even in steady state a fraction of frames on a desktop run several times the
// median because the compositor preempts the GPU; that is interference, not
// pass cost. Across those same five runs the spread is 1.9% on the minimum,
// 4.7% on a median and 67.7% on a p90 -- so a high percentile, which is what
// 3D_E-0626 first guessed at, would gate on the interference rather than on the
// pass. Design § 8's rows are uncontended per-pass cost, so that is the figure
// they mean. The tier row is the exception: it is derived from a frame share
// rather than measured, and § 8 owns why a ceiling of that kind is policed by
// a minimum.
//
// Each gate reports min, median and max, so the spread is visible in the log
// rather than inferred from one number.
struct BenchResult
{
    double gated  = 0.0;  // the minimum — what the budgets are asserted against
    double median = 0.0;
    double max    = 0.0;
    int    frames = 0;
};

// Sustained load before the first timed frame. Covers the slowest clock ramp in
// the estate (the GTX 1050's, above) with margin.
inline constexpr double kBenchWarmupMillis = 600.0;
// Floor for a pass slow enough that the millisecond budget buys too few frames.
inline constexpr int kBenchWarmupFramesMin = 16;
// Timed frames. Fewer than this widens the run-to-run spread on the minimum;
// more does not narrow it.
inline constexpr int kBenchTimedFrames = 128;

/// Warm past the clock ramp, then time `timeOnce` repeatedly. `timeOnce`
/// returns the wall-clock microseconds of one run (for a GPU pass, one
/// glFinish-bracketed dispatch). `gating` is true when the caller will assert
/// a budget on the result; otherwise the run only proves the path works, so it
/// takes a short sample instead of the full warm-up.
template <typename TimeOnce>
BenchResult runBench(TimeOnce timeOnce, bool gating)
{
    if (!gating)
    {
        for (int i = 0; i < 3; ++i) timeOnce();
    }
    else
    {
        const auto warmStart = std::chrono::steady_clock::now();
        for (int i = 0;; ++i)
        {
            timeOnce();
            const double elapsed = std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - warmStart).count();
            if (i + 1 >= kBenchWarmupFramesMin && elapsed >= kBenchWarmupMillis) break;
        }
    }

    const int frames = gating ? kBenchTimedFrames : 8;
    std::vector<double> micros;
    micros.reserve(static_cast<std::size_t>(frames));
    for (int f = 0; f < frames; ++f) micros.push_back(timeOnce());
    std::sort(micros.begin(), micros.end());

    BenchResult r;
    r.gated  = micros.front();
    r.median = micros[micros.size() / 2];
    r.max    = micros.back();
    r.frames = frames;
    return r;
}

// "1454.0 µs (min of 128 timed frames; median 1535.2, max 5470.8)"
inline std::string benchSummary(const BenchResult& r)
{
    std::ostringstream os;
    os << r.gated << " µs (min of " << r.frames << " timed frames; median "
       << r.median << ", max " << r.max << ")";
    return os.str();
}

} // namespace Vestige::Test

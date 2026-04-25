// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "renderer/ibl_capture_sequence.h"

namespace Vestige
{

void runIblCaptureSequence(std::initializer_list<std::function<void()>> steps)
{
    // RED stub — production overload exists so callers compile, but the
    // ScopedForwardZ bracket isn't established yet. Replaced in the green
    // commit by `runIblCaptureSequenceWith<ScopedForwardZ>(steps)`.
    for (const auto& step : steps)
    {
        if (step) step();
    }
}

} // namespace Vestige

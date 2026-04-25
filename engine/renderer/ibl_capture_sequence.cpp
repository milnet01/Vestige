// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "renderer/ibl_capture_sequence.h"

#include "renderer/scoped_forward_z.h"

namespace Vestige
{

void runIblCaptureSequence(std::initializer_list<std::function<void()>> steps)
{
    runIblCaptureSequenceWith<ScopedForwardZ>(steps);
}

} // namespace Vestige

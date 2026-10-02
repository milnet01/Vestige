// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file anti_alias_mode.h
/// @brief The anti-aliasing mode enum, on its own so GL-free code (saved
///        settings, 3D_E-0035) can name it without the TAA pass's headers.
#pragma once

namespace Vestige
{

/// @brief Anti-aliasing mode selection.
enum class AntiAliasMode
{
    NONE,
    MSAA_4X,
    TAA,
    SMAA,
    FXAA    ///< Tier-1 budget post-process AA (Lottes FXAA 3.11). Appended
            ///< last so existing serialized AA-mode ints stay stable.
};

} // namespace Vestige

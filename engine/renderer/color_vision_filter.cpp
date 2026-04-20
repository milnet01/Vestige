// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file color_vision_filter.cpp
/// @brief CVD matrix lookup tables (Viénot/Brettel/Mollon 1999).
#include "renderer/color_vision_filter.h"

namespace Vestige
{

// GLM stores mat3 column-major: the nested initializer lists each
// describe a column, not a row. Values below are transcribed from
// Viénot/Brettel/Mollon 1999, Table 3 (dichromat projection matrices).
// Rows in the paper are:
//     [ a b c ]
//     [ d e f ]
//     [ g h i ]
// which becomes glm::mat3({a,d,g},{b,e,h},{c,f,i}).

glm::mat3 colorVisionMatrix(ColorVisionMode mode)
{
    switch (mode)
    {
        case ColorVisionMode::Normal:
            return glm::mat3(1.0f);

        case ColorVisionMode::Protanopia:
            return glm::mat3(
                {0.56667f, 0.55833f, 0.00000f},
                {0.43333f, 0.44167f, 0.24167f},
                {0.00000f, 0.00000f, 0.75833f});

        case ColorVisionMode::Deuteranopia:
            return glm::mat3(
                {0.62500f, 0.70000f, 0.00000f},
                {0.37500f, 0.30000f, 0.30000f},
                {0.00000f, 0.00000f, 0.70000f});

        case ColorVisionMode::Tritanopia:
            return glm::mat3(
                {0.95000f, 0.00000f, 0.00000f},
                {0.05000f, 0.43333f, 0.47500f},
                {0.00000f, 0.56667f, 0.52500f});
    }
    return glm::mat3(1.0f);
}

const char* colorVisionModeLabel(ColorVisionMode mode)
{
    switch (mode)
    {
        case ColorVisionMode::Normal:       return "Normal";
        case ColorVisionMode::Protanopia:   return "Protanopia";
        case ColorVisionMode::Deuteranopia: return "Deuteranopia";
        case ColorVisionMode::Tritanopia:   return "Tritanopia";
    }
    return "Normal";
}

} // namespace Vestige

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_accessible.cpp
/// @brief Role-label lookup for the accessibility layer.

#include "ui/ui_accessible.h"

namespace Vestige
{

const char* uiAccessibleRoleLabel(UIAccessibleRole role)
{
    switch (role)
    {
        case UIAccessibleRole::Unknown:     return "Unknown";
        case UIAccessibleRole::Label:       return "Label";
        case UIAccessibleRole::Panel:       return "Panel";
        case UIAccessibleRole::Image:       return "Image";
        case UIAccessibleRole::Button:      return "Button";
        case UIAccessibleRole::Checkbox:    return "Checkbox";
        case UIAccessibleRole::Slider:      return "Slider";
        case UIAccessibleRole::Dropdown:    return "Dropdown";
        case UIAccessibleRole::KeybindRow:  return "KeybindRow";
        case UIAccessibleRole::ProgressBar: return "ProgressBar";
        case UIAccessibleRole::Crosshair:   return "Crosshair";
    }
    return "Unknown";
}

} // namespace Vestige

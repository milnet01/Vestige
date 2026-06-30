// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_device_hotswap.cpp
/// @brief AX11 device-hot-swap policy decision + enum string mapping.
#include "audio/audio_device_hotswap.h"

namespace Vestige
{

DeviceSwapAction decideDeviceSwapAction(DeviceHotSwapMode mode)
{
    switch (mode)
    {
        case DeviceHotSwapMode::Off:    return DeviceSwapAction::Ignore;
        case DeviceHotSwapMode::Notify: return DeviceSwapAction::Notify;
        case DeviceHotSwapMode::Auto:   return DeviceSwapAction::Swap;
    }
    return DeviceSwapAction::Ignore;
}

const char* deviceHotSwapModeLabel(DeviceHotSwapMode mode)
{
    switch (mode)
    {
        case DeviceHotSwapMode::Off:    return "Off";
        case DeviceHotSwapMode::Notify: return "Notify";
        case DeviceHotSwapMode::Auto:   return "Auto";
    }
    return "Notify";
}

std::string deviceHotSwapModeToString(DeviceHotSwapMode mode)
{
    switch (mode)
    {
        case DeviceHotSwapMode::Off:    return "off";
        case DeviceHotSwapMode::Notify: return "notify";
        case DeviceHotSwapMode::Auto:   return "auto";
    }
    return "notify";
}

DeviceHotSwapMode deviceHotSwapModeFromString(const std::string& s,
                                              DeviceHotSwapMode fallback)
{
    if (s == "off")    return DeviceHotSwapMode::Off;
    if (s == "notify") return DeviceHotSwapMode::Notify;
    if (s == "auto")   return DeviceHotSwapMode::Auto;
    return fallback;
}

} // namespace Vestige

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_output_mode.cpp
/// @brief AX8 speaker-layout enum labels + `ALC_OUTPUT_MODE_SOFT` mapping.

#include "audio/audio_output_mode.h"

#include <AL/alc.h>
#include <AL/alext.h>

namespace Vestige
{

const char* audioOutputLayoutLabel(AudioOutputLayout layout)
{
    switch (layout)
    {
        case AudioOutputLayout::Auto:       return "Auto";
        case AudioOutputLayout::Mono:       return "Mono";
        case AudioOutputLayout::Stereo:     return "Stereo";
        case AudioOutputLayout::Surround51: return "5.1 Surround";
        case AudioOutputLayout::Surround71: return "7.1 Surround";
    }
    return "Auto";
}

std::string audioOutputLayoutToString(AudioOutputLayout layout)
{
    switch (layout)
    {
        case AudioOutputLayout::Auto:       return "auto";
        case AudioOutputLayout::Mono:       return "mono";
        case AudioOutputLayout::Stereo:     return "stereo";
        case AudioOutputLayout::Surround51: return "surround51";
        case AudioOutputLayout::Surround71: return "surround71";
    }
    return "auto";
}

AudioOutputLayout audioOutputLayoutFromString(const std::string& s, AudioOutputLayout fallback)
{
    if (s == "auto")       return AudioOutputLayout::Auto;
    if (s == "mono")       return AudioOutputLayout::Mono;
    if (s == "stereo")     return AudioOutputLayout::Stereo;
    if (s == "surround51") return AudioOutputLayout::Surround51;
    if (s == "surround71") return AudioOutputLayout::Surround71;
    return fallback;
}

int resolveOutputMode(AudioOutputLayout layout, bool hrtfEnabledSetting)
{
    // HRTF wins: when HRTF is on, do not request a surround layout —
    // return ALC_ANY_SOFT so the driver resolves stereo-HRTF on
    // headphones via the separate ALC_HRTF_SOFT attribute.
    if (hrtfEnabledSetting)
    {
        return ALC_ANY_SOFT;
    }

    switch (layout)
    {
        case AudioOutputLayout::Auto:       return ALC_ANY_SOFT;
        case AudioOutputLayout::Mono:       return ALC_MONO_SOFT;
        case AudioOutputLayout::Stereo:     return ALC_STEREO_SOFT;
        case AudioOutputLayout::Surround51: return ALC_5POINT1_SOFT;
        case AudioOutputLayout::Surround71: return ALC_7POINT1_SOFT;
    }
    return ALC_ANY_SOFT;
}

} // namespace Vestige

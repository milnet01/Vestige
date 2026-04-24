// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_hrtf.cpp
/// @brief HRTF mode / status labels + dataset-name resolution.
#include "audio/audio_hrtf.h"

namespace Vestige
{

const char* hrtfModeLabel(HrtfMode mode)
{
    switch (mode)
    {
        case HrtfMode::Disabled: return "Disabled";
        case HrtfMode::Auto:     return "Auto";
        case HrtfMode::Forced:   return "Forced";
    }
    return "Unknown";
}

const char* hrtfStatusLabel(HrtfStatus status)
{
    switch (status)
    {
        case HrtfStatus::Disabled:           return "Disabled";
        case HrtfStatus::Enabled:            return "Enabled";
        case HrtfStatus::Denied:             return "Denied";
        case HrtfStatus::Required:           return "Required";
        case HrtfStatus::HeadphonesDetected: return "HeadphonesDetected";
        case HrtfStatus::UnsupportedFormat:  return "UnsupportedFormat";
        case HrtfStatus::Unknown:            return "Unknown";
    }
    return "Unknown";
}

HrtfStatusEvent composeHrtfStatusEvent(const HrtfSettings& settings,
                                       HrtfStatus actualStatus)
{
    HrtfStatusEvent event;
    event.requestedMode    = settings.mode;
    event.requestedDataset = settings.preferredDataset;
    event.actualStatus     = actualStatus;
    return event;
}

int resolveHrtfDatasetIndex(const std::vector<std::string>& available,
                            const std::string& preferred)
{
    if (available.empty())
    {
        return -1;
    }
    if (preferred.empty())
    {
        return 0;
    }
    for (size_t i = 0; i < available.size(); ++i)
    {
        if (available[i] == preferred)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace Vestige

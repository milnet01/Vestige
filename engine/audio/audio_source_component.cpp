// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_source_component.cpp
/// @brief AudioSourceComponent implementation.
#include "audio/audio_source_component.h"

namespace Vestige
{

std::unique_ptr<Component> AudioSourceComponent::clone() const
{
    auto copy = std::make_unique<AudioSourceComponent>();
    copy->clipPath          = clipPath;
    copy->volume            = volume;
    copy->bus               = bus;
    copy->pitch             = pitch;
    copy->minDistance       = minDistance;
    copy->maxDistance       = maxDistance;
    copy->rolloffFactor     = rolloffFactor;
    copy->attenuationModel  = attenuationModel;
    copy->velocity          = velocity;
    copy->occlusionMaterial = occlusionMaterial;
    copy->occlusionFraction = occlusionFraction;
    copy->loop              = loop;
    copy->autoPlay          = autoPlay;
    copy->spatial           = spatial;
    return copy;
}

} // namespace Vestige

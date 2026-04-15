// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file particle_presets.cpp
/// @brief Factory methods for common particle effect configurations.
#include "scene/particle_presets.h"

namespace Vestige
{

ParticleEmitterConfig ParticlePresets::torchFire()
{
    ParticleEmitterConfig c;
    c.emissionRate = 80.0f;
    c.maxParticles = 200;
    c.looping = true;

    c.startLifetimeMin = 0.3f;
    c.startLifetimeMax = 0.8f;
    c.startSpeedMin = 1.0f;
    c.startSpeedMax = 2.5f;
    c.startSizeMin = 0.04f;
    c.startSizeMax = 0.15f;
    c.startColor = {1.0f, 0.8f, 0.3f, 1.0f};

    c.gravity = {0.0f, 1.0f, 0.0f}; // Slight upward buoyancy
    c.shape = ParticleEmitterConfig::Shape::CONE;
    c.shapeRadius = 0.05f;
    c.shapeConeAngle = 15.0f;

    c.useColorOverLifetime = true;
    c.colorOverLifetime = ColorGradient();
    c.colorOverLifetime.stops.clear();
    c.colorOverLifetime.addStop(0.0f, {1.0f, 0.95f, 0.6f, 1.0f});  // Bright yellow-white
    c.colorOverLifetime.addStop(0.3f, {1.0f, 0.6f, 0.1f, 0.9f});   // Orange
    c.colorOverLifetime.addStop(0.7f, {0.8f, 0.2f, 0.05f, 0.6f});  // Dark red
    c.colorOverLifetime.addStop(1.0f, {0.3f, 0.05f, 0.0f, 0.0f});  // Fade out

    c.useSizeOverLifetime = true;
    c.sizeOverLifetime = AnimationCurve();
    c.sizeOverLifetime.keyframes.clear();
    c.sizeOverLifetime.addKeyframe(0.0f, 0.5f);
    c.sizeOverLifetime.addKeyframe(0.3f, 1.0f);
    c.sizeOverLifetime.addKeyframe(1.0f, 0.2f);

    c.blendMode = ParticleEmitterConfig::BlendMode::ADDITIVE;

    c.emitsLight = true;
    c.lightColor = {1.0f, 0.6f, 0.2f};
    c.lightRange = 8.0f;
    c.lightIntensity = 1.5f;
    c.flickerSpeed = 12.0f;

    return c;
}

ParticleEmitterConfig ParticlePresets::candleFlame()
{
    ParticleEmitterConfig c;
    c.emissionRate = 30.0f;
    c.maxParticles = 50;
    c.looping = true;

    c.startLifetimeMin = 0.2f;
    c.startLifetimeMax = 0.5f;
    c.startSpeedMin = 0.3f;
    c.startSpeedMax = 0.8f;
    c.startSizeMin = 0.01f;
    c.startSizeMax = 0.04f;
    c.startColor = {1.0f, 0.9f, 0.4f, 1.0f};

    c.gravity = {0.0f, 0.5f, 0.0f};
    c.shape = ParticleEmitterConfig::Shape::POINT;

    c.useColorOverLifetime = true;
    c.colorOverLifetime = ColorGradient();
    c.colorOverLifetime.stops.clear();
    c.colorOverLifetime.addStop(0.0f, {1.0f, 0.95f, 0.7f, 1.0f});
    c.colorOverLifetime.addStop(0.5f, {1.0f, 0.7f, 0.2f, 0.8f});
    c.colorOverLifetime.addStop(1.0f, {0.8f, 0.3f, 0.0f, 0.0f});

    c.blendMode = ParticleEmitterConfig::BlendMode::ADDITIVE;

    c.emitsLight = true;
    c.lightColor = {1.0f, 0.8f, 0.4f};
    c.lightRange = 3.0f;
    c.lightIntensity = 0.6f;
    c.flickerSpeed = 15.0f;

    return c;
}

ParticleEmitterConfig ParticlePresets::campfire()
{
    ParticleEmitterConfig c;
    c.emissionRate = 150.0f;
    c.maxParticles = 500;
    c.looping = true;

    c.startLifetimeMin = 0.5f;
    c.startLifetimeMax = 1.5f;
    c.startSpeedMin = 1.5f;
    c.startSpeedMax = 3.5f;
    c.startSizeMin = 0.05f;
    c.startSizeMax = 0.25f;
    c.startColor = {1.0f, 0.7f, 0.2f, 1.0f};

    c.gravity = {0.0f, 0.8f, 0.0f};
    c.shape = ParticleEmitterConfig::Shape::CONE;
    c.shapeRadius = 0.3f;
    c.shapeConeAngle = 30.0f;

    c.useColorOverLifetime = true;
    c.colorOverLifetime = ColorGradient();
    c.colorOverLifetime.stops.clear();
    c.colorOverLifetime.addStop(0.0f, {1.0f, 0.9f, 0.5f, 1.0f});
    c.colorOverLifetime.addStop(0.2f, {1.0f, 0.6f, 0.1f, 0.9f});
    c.colorOverLifetime.addStop(0.6f, {0.7f, 0.15f, 0.02f, 0.5f});
    c.colorOverLifetime.addStop(1.0f, {0.2f, 0.02f, 0.0f, 0.0f});

    c.useSizeOverLifetime = true;
    c.sizeOverLifetime = AnimationCurve();
    c.sizeOverLifetime.keyframes.clear();
    c.sizeOverLifetime.addKeyframe(0.0f, 0.3f);
    c.sizeOverLifetime.addKeyframe(0.2f, 1.0f);
    c.sizeOverLifetime.addKeyframe(1.0f, 0.1f);

    c.blendMode = ParticleEmitterConfig::BlendMode::ADDITIVE;

    c.emitsLight = true;
    c.lightColor = {1.0f, 0.5f, 0.15f};
    c.lightRange = 12.0f;
    c.lightIntensity = 2.0f;
    c.flickerSpeed = 8.0f;

    return c;
}

ParticleEmitterConfig ParticlePresets::smoke()
{
    ParticleEmitterConfig c;
    c.emissionRate = 20.0f;
    c.maxParticles = 100;
    c.looping = true;

    c.startLifetimeMin = 2.0f;
    c.startLifetimeMax = 4.0f;
    c.startSpeedMin = 0.5f;
    c.startSpeedMax = 1.5f;
    c.startSizeMin = 0.1f;
    c.startSizeMax = 0.3f;
    c.startColor = {0.5f, 0.5f, 0.5f, 0.6f};

    c.gravity = {0.0f, 0.3f, 0.0f}; // Slow upward drift
    c.shape = ParticleEmitterConfig::Shape::CONE;
    c.shapeRadius = 0.1f;
    c.shapeConeAngle = 20.0f;

    c.useColorOverLifetime = true;
    c.colorOverLifetime = ColorGradient();
    c.colorOverLifetime.stops.clear();
    c.colorOverLifetime.addStop(0.0f, {0.6f, 0.6f, 0.6f, 0.5f});
    c.colorOverLifetime.addStop(0.5f, {0.5f, 0.5f, 0.5f, 0.3f});
    c.colorOverLifetime.addStop(1.0f, {0.4f, 0.4f, 0.4f, 0.0f});

    c.useSizeOverLifetime = true;
    c.sizeOverLifetime = AnimationCurve();
    c.sizeOverLifetime.keyframes.clear();
    c.sizeOverLifetime.addKeyframe(0.0f, 1.0f);
    c.sizeOverLifetime.addKeyframe(1.0f, 4.0f); // Expand significantly

    c.blendMode = ParticleEmitterConfig::BlendMode::ALPHA_BLEND;

    return c;
}

ParticleEmitterConfig ParticlePresets::dustMotes()
{
    ParticleEmitterConfig c;
    c.emissionRate = 50.0f;
    c.maxParticles = 2000;
    c.looping = true;

    c.startLifetimeMin = 5.0f;
    c.startLifetimeMax = 15.0f;
    c.startSpeedMin = 0.02f;
    c.startSpeedMax = 0.08f;
    c.startSizeMin = 0.002f;
    c.startSizeMax = 0.005f;
    c.startColor = {1.0f, 0.95f, 0.85f, 0.4f};

    c.gravity = {0.0f, -0.01f, 0.0f}; // Nearly weightless
    c.shape = ParticleEmitterConfig::Shape::BOX;
    c.shapeBoxSize = {8.0f, 4.0f, 8.0f}; // Large room volume

    c.useColorOverLifetime = true;
    c.colorOverLifetime = ColorGradient();
    c.colorOverLifetime.stops.clear();
    c.colorOverLifetime.addStop(0.0f, {1.0f, 0.95f, 0.85f, 0.0f}); // Fade in
    c.colorOverLifetime.addStop(0.1f, {1.0f, 0.95f, 0.85f, 0.4f});
    c.colorOverLifetime.addStop(0.9f, {1.0f, 0.95f, 0.85f, 0.4f});
    c.colorOverLifetime.addStop(1.0f, {1.0f, 0.95f, 0.85f, 0.0f}); // Fade out

    c.blendMode = ParticleEmitterConfig::BlendMode::ADDITIVE;

    return c;
}

ParticleEmitterConfig ParticlePresets::incense()
{
    ParticleEmitterConfig c;
    c.emissionRate = 40.0f;
    c.maxParticles = 200;
    c.looping = true;

    c.startLifetimeMin = 3.0f;
    c.startLifetimeMax = 6.0f;
    c.startSpeedMin = 0.3f;
    c.startSpeedMax = 0.5f;
    c.startSizeMin = 0.01f;
    c.startSizeMax = 0.02f;
    c.startColor = {0.85f, 0.85f, 0.9f, 0.5f};

    c.gravity = {0.0f, 0.15f, 0.0f}; // Gentle upward
    c.shape = ParticleEmitterConfig::Shape::POINT;

    c.useColorOverLifetime = true;
    c.colorOverLifetime = ColorGradient();
    c.colorOverLifetime.stops.clear();
    c.colorOverLifetime.addStop(0.0f, {0.9f, 0.9f, 0.95f, 0.6f});
    c.colorOverLifetime.addStop(0.3f, {0.8f, 0.8f, 0.85f, 0.4f});
    c.colorOverLifetime.addStop(0.7f, {0.7f, 0.7f, 0.75f, 0.15f});
    c.colorOverLifetime.addStop(1.0f, {0.6f, 0.6f, 0.65f, 0.0f});

    c.useSizeOverLifetime = true;
    c.sizeOverLifetime = AnimationCurve();
    c.sizeOverLifetime.keyframes.clear();
    c.sizeOverLifetime.addKeyframe(0.0f, 1.0f);
    c.sizeOverLifetime.addKeyframe(0.3f, 1.5f);
    c.sizeOverLifetime.addKeyframe(1.0f, 5.0f); // Expand as it disperses

    c.useSpeedOverLifetime = true;
    c.speedOverLifetime = AnimationCurve();
    c.speedOverLifetime.keyframes.clear();
    c.speedOverLifetime.addKeyframe(0.0f, 1.0f);
    c.speedOverLifetime.addKeyframe(0.3f, 0.7f);
    c.speedOverLifetime.addKeyframe(1.0f, 0.2f); // Slow down

    c.blendMode = ParticleEmitterConfig::BlendMode::ALPHA_BLEND;

    return c;
}

ParticleEmitterConfig ParticlePresets::sparks()
{
    ParticleEmitterConfig c;
    c.emissionRate = 30.0f;
    c.maxParticles = 100;
    c.looping = true;

    c.startLifetimeMin = 0.5f;
    c.startLifetimeMax = 1.5f;
    c.startSpeedMin = 2.0f;
    c.startSpeedMax = 5.0f;
    c.startSizeMin = 0.005f;
    c.startSizeMax = 0.015f;
    c.startColor = {1.0f, 0.8f, 0.3f, 1.0f};

    c.gravity = {0.0f, -4.0f, 0.0f}; // Moderate gravity for arcing
    c.shape = ParticleEmitterConfig::Shape::SPHERE;
    c.shapeRadius = 0.1f;

    c.useColorOverLifetime = true;
    c.colorOverLifetime = ColorGradient();
    c.colorOverLifetime.stops.clear();
    c.colorOverLifetime.addStop(0.0f, {1.0f, 0.9f, 0.5f, 1.0f});
    c.colorOverLifetime.addStop(0.5f, {1.0f, 0.5f, 0.1f, 0.8f});
    c.colorOverLifetime.addStop(1.0f, {0.5f, 0.1f, 0.0f, 0.0f});

    c.blendMode = ParticleEmitterConfig::BlendMode::ADDITIVE;

    return c;
}

} // namespace Vestige

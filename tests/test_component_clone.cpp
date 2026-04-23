// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_component_clone.cpp
/// @brief Phase 10.9 Slice 1 F2 — `Component::clone()` completeness contract.
///
/// Contract (authored from ROADMAP Phase 10.9 Slice 1 F2 and from the
/// `Component::clone()` header docstring at engine/scene/component.h):
///
///   "Creates a deep copy of this component (new instance, same data).
///    Override in derived classes to support entity duplication."
///
/// Entity::clone() at engine/scene/entity.cpp:200 iterates every
/// owned component and calls `comp->clone()`; if the result is null
/// the component is silently dropped from the duplicate. That silent
/// drop is the F2 footgun — any concrete component that forgets to
/// override `clone()` inherits the base's nullptr-returning default
/// and vanishes on duplicate/paste with no error.
///
/// This test pins the invariant: every concrete `Component` subclass
/// must return a non-null deep copy from `clone()`. Subclasses whose
/// constructors take no arguments are exercised directly here.
/// Subclasses with mandatory constructor arguments (e.g. `RigidBody`
/// taking a PhysicsWorld) already have clone coverage alongside
/// their owning subsystem tests and are out of scope for the F2
/// focused regression.

#include <gtest/gtest.h>

#include "animation/facial_animation.h"
#include "animation/lip_sync.h"
#include "animation/tween.h"
#include "audio/audio_source_component.h"
#include "physics/cloth_component.h"
#include "scene/camera_2d_component.h"
#include "scene/camera_component.h"
#include "scene/character_controller_2d_component.h"
#include "scene/collider_2d_component.h"
#include "scene/gpu_particle_emitter.h"
#include "scene/interactable_component.h"
#include "scene/light_component.h"
#include "scene/mesh_renderer.h"
#include "scene/particle_emitter.h"
#include "scene/pressure_plate_component.h"
#include "scene/rigid_body_2d_component.h"
#include "scene/sprite_component.h"
#include "scene/tilemap_component.h"
#include "scene/water_surface.h"

using namespace Vestige;

// Each TEST case constructs a default instance of a concrete
// `Component` subclass, calls `clone()`, and asserts the result is
// both non-null and of the expected derived type. A silently-dropped
// clone (nullptr-returning base) fails at the ASSERT_NE; a clone that
// returns the wrong dynamic type fails at the dynamic_cast.
//
// The ClothComponent case is the one the F2 roadmap item explicitly
// calls out — before the green commit it fails; the other cases act
// as a lock against future silent-drop regressions.

TEST(ComponentClone, ClothComponentReturnsNonNull)
{
    ClothComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<ClothComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, MeshRendererReturnsNonNull)
{
    MeshRenderer comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<MeshRenderer*>(copy.get()), nullptr);
}

TEST(ComponentClone, DirectionalLightReturnsNonNull)
{
    DirectionalLightComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<DirectionalLightComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, PointLightReturnsNonNull)
{
    PointLightComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<PointLightComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, SpotLightReturnsNonNull)
{
    SpotLightComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<SpotLightComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, EmissiveLightReturnsNonNull)
{
    EmissiveLightComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<EmissiveLightComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, CameraComponentReturnsNonNull)
{
    CameraComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<CameraComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, Camera2DComponentReturnsNonNull)
{
    Camera2DComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<Camera2DComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, SpriteComponentReturnsNonNull)
{
    SpriteComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<SpriteComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, TilemapComponentReturnsNonNull)
{
    TilemapComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<TilemapComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, Collider2DComponentReturnsNonNull)
{
    Collider2DComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<Collider2DComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, RigidBody2DComponentReturnsNonNull)
{
    RigidBody2DComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<RigidBody2DComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, CharacterController2DReturnsNonNull)
{
    CharacterController2DComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<CharacterController2DComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, ParticleEmitterReturnsNonNull)
{
    ParticleEmitterComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<ParticleEmitterComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, GPUParticleEmitterReturnsNonNull)
{
    GPUParticleEmitter comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<GPUParticleEmitter*>(copy.get()), nullptr);
}

TEST(ComponentClone, WaterSurfaceReturnsNonNull)
{
    WaterSurfaceComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<WaterSurfaceComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, PressurePlateReturnsNonNull)
{
    PressurePlateComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<PressurePlateComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, InteractableReturnsNonNull)
{
    InteractableComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<InteractableComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, AudioSourceReturnsNonNull)
{
    AudioSourceComponent comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<AudioSourceComponent*>(copy.get()), nullptr);
}

TEST(ComponentClone, FacialAnimatorReturnsNonNull)
{
    FacialAnimator comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<FacialAnimator*>(copy.get()), nullptr);
}

TEST(ComponentClone, LipSyncPlayerReturnsNonNull)
{
    LipSyncPlayer comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<LipSyncPlayer*>(copy.get()), nullptr);
}

TEST(ComponentClone, TweenManagerReturnsNonNull)
{
    TweenManager comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<TweenManager*>(copy.get()), nullptr);
}

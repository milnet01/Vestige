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
/// must return a non-null deep copy from `clone()` AND the result's
/// dynamic type must match the source.
///
/// Phase 10.9 Slice 18 Ts4 consolidation: 22 structurally-identical
/// `TEST(ComponentClone, *ReturnsNonNull)` cases collapsed to one
/// typed test with 22 instantiations. Adding a 23rd component is now
/// one line (`AddTypedTestType<T>(...)`) instead of a 6-line block.
/// The contract under test is unchanged.
///
/// The ClothComponent case is the one the F2 roadmap item explicitly
/// calls out — before the green commit it fails; the other cases act
/// as a lock against future silent-drop regressions.

#include <gtest/gtest.h>

#include "experimental/animation/facial_animation.h"
#include "experimental/animation/lip_sync.h"
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

template <typename T>
class ComponentCloneTest : public ::testing::Test {};

using CloneableComponentTypes = ::testing::Types<
    ClothComponent,
    MeshRenderer,
    DirectionalLightComponent,
    PointLightComponent,
    SpotLightComponent,
    EmissiveLightComponent,
    CameraComponent,
    Camera2DComponent,
    SpriteComponent,
    TilemapComponent,
    Collider2DComponent,
    RigidBody2DComponent,
    CharacterController2DComponent,
    ParticleEmitterComponent,
    GPUParticleEmitter,
    WaterSurfaceComponent,
    PressurePlateComponent,
    InteractableComponent,
    AudioSourceComponent,
    FacialAnimator,
    LipSyncPlayer,
    TweenManager
>;
TYPED_TEST_SUITE(ComponentCloneTest, CloneableComponentTypes);

TYPED_TEST(ComponentCloneTest, DefaultInstanceClonesToSameDynamicType)
{
    TypeParam comp;
    auto copy = comp.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<TypeParam*>(copy.get()), nullptr);
}

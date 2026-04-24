// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_entity_serializer_registry.cpp
/// @brief Phase 10.9 Slice 1 F3 — `ComponentSerializerRegistry`
///        contract: components register their own JSON read/write
///        rather than being dropped by a fixed allowlist.
///
/// Contract (authored from ROADMAP Phase 10.9 Slice 1 F3 and from
/// the `AudioSourceComponent::bus` docstring at
/// engine/audio/audio_source_component.h:35-49, which references the
/// Phase 10.7 slice A1 mixer-bus design):
///
///   "Every source now belongs to exactly one non-Master bus... The
///    per-frame gain resolution in AudioSystem composes the effective
///    gain as `master × bus × volume × occlusion × ducking`, so a
///    user changing Music gain in Settings attenuates every source
///    tagged `AudioBus::Music`..."
///
/// Before F3 the entity serializer carried a fixed 7-entry allowlist
/// (MeshRenderer, DirectionalLight, PointLight, SpotLight,
/// EmissiveLight, ParticleEmitter, WaterSurface). Any `AudioSource`
/// placed on a scene entity was silently dropped on save — the Phase
/// 10.7 A1 bus assignment never survived a round-trip, so a scene
/// file's Music-bus ambient track came back as a Sfx-bus default.
///
/// These tests pin the round-trip: every field on
/// `AudioSourceComponent` that a user can set via the editor must
/// survive `serializeEntity` → `deserializeEntity`. The red commit
/// exists to prove the fixed allowlist's silent-drop; the green
/// commit introduces the registry and registers `AudioSource`.

#include <gtest/gtest.h>

#include "audio/audio_source_component.h"
#include "resource/resource_manager.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "utils/entity_serializer.h"

#include <nlohmann/json.hpp>

using namespace Vestige;
using json = nlohmann::json;

namespace
{

// Build an AudioSourceComponent with distinctive, non-default values
// across every serialisable field, so a partial serializer (one that
// writes only a subset of fields) still fails the round-trip.
void populateDistinctive(AudioSourceComponent& asc)
{
    asc.clipPath          = "audio/music/theme.ogg";
    asc.volume            = 0.75f;
    asc.bus               = AudioBus::Music;
    asc.pitch             = 0.9f;
    asc.minDistance       = 2.0f;
    asc.maxDistance       = 30.0f;
    asc.rolloffFactor     = 1.5f;
    asc.attenuationModel  = AttenuationModel::Exponential;
    asc.velocity          = glm::vec3(1.0f, 2.0f, 3.0f);
    asc.occlusionMaterial = AudioOcclusionMaterialPreset::Stone;
    asc.occlusionFraction = 0.4f;
    asc.loop              = true;
    asc.autoPlay          = true;
    asc.spatial           = false;
    asc.priority          = SoundPriority::Critical;  // Phase 10.9 P7
}

} // namespace

// ---------------------------------------------------------------------------
// Serialisation emits an AudioSource entry at all
// ---------------------------------------------------------------------------

TEST(EntitySerializerRegistry, AudioSourceAppearsInSerializedJson)
{
    Scene scene("SerializeTest");
    Entity* host = scene.createEntity("AudioHost");
    auto* asc = host->addComponent<AudioSourceComponent>();
    populateDistinctive(*asc);

    ResourceManager resources;  // No GPU calls exercised by AudioSource path.
    json j = EntitySerializer::serializeEntity(*host, resources);

    // Fixed-allowlist serializer drops AudioSource: the `components`
    // dict either omits it or isn't created at all. After F3 the
    // registry-backed serializer must emit an `AudioSource` entry.
    ASSERT_TRUE(j.contains("components"))
        << "components dict missing — AudioSource never serialised";
    ASSERT_TRUE(j["components"].contains("AudioSource"))
        << "AudioSource silently dropped by fixed allowlist";
}

// ---------------------------------------------------------------------------
// Phase 10.7 A1 bus field survives the JSON round-trip
// ---------------------------------------------------------------------------

TEST(EntitySerializerRegistry, AudioSourceBusRoundTrips_Phase10_7_A1)
{
    Scene sceneOut("Out");
    Entity* src = sceneOut.createEntity("AudioHost");
    auto* ascOut = src->addComponent<AudioSourceComponent>();
    ascOut->clipPath = "audio/music/theme.ogg";
    ascOut->bus      = AudioBus::Music;  // Not the default Sfx.

    ResourceManager resources;
    json j = EntitySerializer::serializeEntity(*src, resources);

    Scene sceneIn("In");
    Entity* dst = EntitySerializer::deserializeEntity(j, sceneIn, resources);
    ASSERT_NE(dst, nullptr);

    auto* ascIn = dst->getComponent<AudioSourceComponent>();
    ASSERT_NE(ascIn, nullptr)
        << "AudioSource dropped on deserialisation — fixed-allowlist silent-drop";
    EXPECT_EQ(ascIn->bus, AudioBus::Music)
        << "Phase 10.7 A1 bus assignment did not survive round-trip; "
           "a Music-tagged scene source deserialised as Sfx default";
    EXPECT_EQ(ascIn->clipPath, "audio/music/theme.ogg");
}

// ---------------------------------------------------------------------------
// Every user-editable field survives the round-trip
// ---------------------------------------------------------------------------

TEST(EntitySerializerRegistry, AudioSourceAllFieldsRoundTrip)
{
    Scene sceneOut("Out");
    Entity* src = sceneOut.createEntity("AudioHost");
    auto* ascOut = src->addComponent<AudioSourceComponent>();
    populateDistinctive(*ascOut);

    ResourceManager resources;
    json j = EntitySerializer::serializeEntity(*src, resources);

    Scene sceneIn("In");
    Entity* dst = EntitySerializer::deserializeEntity(j, sceneIn, resources);
    ASSERT_NE(dst, nullptr);

    auto* ascIn = dst->getComponent<AudioSourceComponent>();
    ASSERT_NE(ascIn, nullptr);

    EXPECT_EQ(ascIn->clipPath,          "audio/music/theme.ogg");
    EXPECT_FLOAT_EQ(ascIn->volume,      0.75f);
    EXPECT_EQ(ascIn->bus,               AudioBus::Music);
    EXPECT_FLOAT_EQ(ascIn->pitch,       0.9f);
    EXPECT_FLOAT_EQ(ascIn->minDistance, 2.0f);
    EXPECT_FLOAT_EQ(ascIn->maxDistance, 30.0f);
    EXPECT_FLOAT_EQ(ascIn->rolloffFactor, 1.5f);
    EXPECT_EQ(ascIn->attenuationModel,  AttenuationModel::Exponential);
    EXPECT_FLOAT_EQ(ascIn->velocity.x,  1.0f);
    EXPECT_FLOAT_EQ(ascIn->velocity.y,  2.0f);
    EXPECT_FLOAT_EQ(ascIn->velocity.z,  3.0f);
    EXPECT_EQ(ascIn->occlusionMaterial, AudioOcclusionMaterialPreset::Stone);
    EXPECT_FLOAT_EQ(ascIn->occlusionFraction, 0.4f);
    EXPECT_TRUE(ascIn->loop);
    EXPECT_TRUE(ascIn->autoPlay);
    EXPECT_FALSE(ascIn->spatial);
    EXPECT_EQ(ascIn->priority, SoundPriority::Critical);
}

// ---------------------------------------------------------------------------
// Absent `bus` field defaults to Sfx (compatibility with pre-A1 scenes)
// ---------------------------------------------------------------------------

TEST(EntitySerializerRegistry, AudioSourceAbsentBusDefaultsToSfx)
{
    // Manually craft a pre-A1-shape JSON with AudioSource present but
    // no `bus` field — matches scenes authored before the mixer-bus
    // design landed. The deserializer must hydrate `bus = Sfx`, the
    // pre-A1 implicit routing, per the docstring at
    // audio_source_component.h:44-48.
    json entityJson;
    entityJson["name"] = "Host";
    entityJson["transform"]["position"] = {0.0f, 0.0f, 0.0f};
    entityJson["transform"]["rotation"] = {0.0f, 0.0f, 0.0f};
    entityJson["transform"]["scale"]    = {1.0f, 1.0f, 1.0f};
    entityJson["components"]["AudioSource"]["clipPath"] = "audio/sfx/click.wav";
    entityJson["components"]["AudioSource"]["volume"]   = 1.0f;
    // Intentionally no `bus` field.

    Scene scene("PreA1");
    ResourceManager resources;
    Entity* host = EntitySerializer::deserializeEntity(entityJson, scene, resources);
    ASSERT_NE(host, nullptr);

    auto* asc = host->getComponent<AudioSourceComponent>();
    ASSERT_NE(asc, nullptr);
    EXPECT_EQ(asc->bus, AudioBus::Sfx)
        << "pre-A1 scene without a `bus` field must default to Sfx "
           "(the implicit routing used before the mixer-bus design)";
    EXPECT_EQ(asc->clipPath, "audio/sfx/click.wav");
}

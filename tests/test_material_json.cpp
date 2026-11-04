// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_material_json.cpp
/// @brief 3D_E-0871 — scenes and material presets save and load the same
///        material fields; only the texture-path rule differs.
///
/// Scene files (EntitySerializer) and presets (MaterialLibrary) each kept a
/// copy of the material JSON code. The scene copy had drifted: it dropped
/// the subsurface fields, so a saved scene lost them. Both paths now round
/// trip the same distinctive material. Scene files stay sandboxed: an
/// absolute texture path in a scene is not loaded.
#include "gl_test_fixture.h"
#include "renderer/material.h"
#include "resource/resource_manager.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "scene/scene.h"
#include "test_helpers.h"
#include "utils/entity_serializer.h"
#include "utils/material_library.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <stb_image_write.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

using namespace Vestige;
using Vestige::Test::GLTestFixture;
using json = nlohmann::json;

namespace
{

class MaterialJsonTest : public GLTestFixture {};

/// Every PBR field a user can set, each away from its default.
void populateDistinctive(Material& m)
{
    m.setType(MaterialType::PBR);
    m.setAlbedo(glm::vec3(0.1f, 0.2f, 0.3f));
    m.setMetallic(0.7f);
    m.setRoughness(0.3f);
    m.setAo(0.6f);
    m.setEmissive(glm::vec3(0.4f, 0.5f, 0.6f));
    m.setEmissiveStrength(2.5f);
    m.setClearcoat(0.8f);
    m.setClearcoatRoughness(0.2f);
    m.setUvScale(3.0f);
    m.setSubsurfaceStrength(0.65f);
    m.setSubsurfaceColor(glm::vec3(0.9f, 0.3f, 0.2f));
    m.setSubsurfaceThickness(0.25f);
    m.setHeightScale(0.08f);
    m.setPomEnabled(true);
    m.setStochasticTiling(true);
    m.setAlphaMode(AlphaMode::MASK);
    m.setAlphaCutoff(0.35f);
    m.setDoubleSided(true);
    m.setBaseColorAlpha(0.75f);
}

void expectDistinctive(const Material& m)
{
    EXPECT_EQ(m.getType(), MaterialType::PBR);
    EXPECT_EQ(m.getAlbedo(), glm::vec3(0.1f, 0.2f, 0.3f));
    EXPECT_FLOAT_EQ(m.getMetallic(), 0.7f);
    EXPECT_FLOAT_EQ(m.getRoughness(), 0.3f);
    EXPECT_FLOAT_EQ(m.getAo(), 0.6f);
    EXPECT_EQ(m.getEmissive(), glm::vec3(0.4f, 0.5f, 0.6f));
    EXPECT_FLOAT_EQ(m.getEmissiveStrength(), 2.5f);
    EXPECT_FLOAT_EQ(m.getClearcoat(), 0.8f);
    EXPECT_FLOAT_EQ(m.getClearcoatRoughness(), 0.2f);
    EXPECT_FLOAT_EQ(m.getUvScale(), 3.0f);
    EXPECT_FLOAT_EQ(m.getSubsurfaceStrength(), 0.65f);
    EXPECT_EQ(m.getSubsurfaceColor(), glm::vec3(0.9f, 0.3f, 0.2f));
    EXPECT_FLOAT_EQ(m.getSubsurfaceThickness(), 0.25f);
    EXPECT_FLOAT_EQ(m.getHeightScale(), 0.08f);
    EXPECT_TRUE(m.isPomEnabled());
    EXPECT_TRUE(m.isStochasticTiling());
    EXPECT_EQ(m.getAlphaMode(), AlphaMode::MASK);
    EXPECT_FLOAT_EQ(m.getAlphaCutoff(), 0.35f);
    EXPECT_TRUE(m.isDoubleSided());
    EXPECT_FLOAT_EQ(m.getBaseColorAlpha(), 0.75f);
}

/// A scene entity's MeshRenderer material, after a save and load.
std::shared_ptr<Material> sceneRoundTrip(const json& entityJson, ResourceManager& resources)
{
    Scene sceneIn("In");
    Entity* dst = EntitySerializer::deserializeEntity(entityJson, sceneIn, resources);
    if (!dst) return nullptr;
    auto* mr = dst->getComponent<MeshRenderer>();
    return mr ? mr->getMaterial() : nullptr;
}

} // namespace

TEST_F(MaterialJsonTest, SceneRoundTripKeepsEveryMaterialField)
{
    ResourceManager resources;
    auto material = resources.createMaterial("distinctive");
    populateDistinctive(*material);

    Scene sceneOut("Out");
    Entity* src = sceneOut.createEntity("Host");
    src->addComponent<MeshRenderer>(resources.getMeshByKey("__builtin_cube"), material);

    const json j = EntitySerializer::serializeEntity(*src, resources);
    auto loaded = sceneRoundTrip(j, resources);
    ASSERT_NE(loaded, nullptr);
    expectDistinctive(*loaded);
}

TEST_F(MaterialJsonTest, PresetRoundTripKeepsEveryMaterialField)
{
    const std::filesystem::path assetDir = std::filesystem::temp_directory_path()
        / ("vestige_material_json_" + Testing::vestigeTestStamp());
    ResourceManager resources;
    Material saved;
    populateDistinctive(saved);

    ASSERT_TRUE(MaterialLibrary::saveMaterial("distinctive", saved, resources, assetDir.string()));
    Material loaded;
    ASSERT_TRUE(MaterialLibrary::loadMaterial("distinctive", loaded, resources, assetDir.string()));
    expectDistinctive(loaded);
    std::filesystem::remove_all(assetDir);
}

TEST_F(MaterialJsonTest, SceneDoesNotLoadAnAbsoluteTexturePath)
{
    // A real, decodable image, so only the path rule can stop it loading.
    const std::string png = (std::filesystem::temp_directory_path()
        / ("vestige_material_json_" + Testing::vestigeTestStamp() + ".png")).string();
    const std::array<std::uint8_t, 4> pixel = {255, 0, 0, 255};
    ASSERT_NE(stbi_write_png(png.c_str(), 1, 1, 4, pixel.data(), 4), 0);

    ResourceManager resources;
    ASSERT_NE(resources.loadTexture(png, false), nullptr) << "fixture image must load";

    json j;
    j["name"] = "Host";
    j["components"]["MeshRenderer"]["mesh"] = "__builtin_cube";
    j["components"]["MeshRenderer"]["material"]["diffuseTexture"] = png;

    auto loaded = sceneRoundTrip(j, resources);
    ASSERT_NE(loaded, nullptr);
    EXPECT_FALSE(loaded->hasDiffuseTexture());
    std::filesystem::remove(png);
}

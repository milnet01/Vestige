// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_procedural_mesh_save.cpp
/// @brief 3D_E-0967: geometry the editor builds (walls, rooms, roofs, stairs,
///        paths) survives a save and load, and a wall keeps every opening cut
///        into it.

#include "editor/command_history.h"
#include "editor/entity_factory.h"
#include "editor/tools/cutout_tool.h"
#include "editor/tools/path_tool.h"
#include "editor/tools/roof_tool.h"
#include "editor/tools/room_tool.h"
#include "editor/tools/stair_tool.h"
#include "editor/tools/wall_tool.h"
#include "gl_test_fixture.h"
#include "resource/resource_manager.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "scene/scene.h"
#include "utils/entity_serializer.h"
#include "utils/mesh_recipe.h"
#include "utils/procedural_mesh.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <functional>
#include <string>
#include <vector>

using namespace Vestige;
using Vestige::Test::GLTestFixture;

namespace
{

class ProceduralMeshSaveTest : public GLTestFixture
{
};

// Every entity in the subtree that has a mesh, in tree order.
void collectMeshed(const Entity& e, std::vector<const Entity*>& out)
{
    const auto* mr = e.getComponent<MeshRenderer>();
    if (mr && mr->getMesh())
    {
        out.push_back(&e);
    }
    for (const auto& child : e.getChildren())
    {
        collectMeshed(*child, out);
    }
}

// INV-1: save then load the subtree, and each meshed entity comes back with a
// mesh of the same local bounds and index count.
void expectSurvivesSaveAndLoad(const Entity& original, ResourceManager& resources)
{
    std::vector<const Entity*> before;
    collectMeshed(original, before);
    ASSERT_FALSE(before.empty()) << "nothing to check: " << original.getName();

    const nlohmann::json j = EntitySerializer::serializeEntity(original, resources);
    Scene sceneIn;
    Entity* loaded = EntitySerializer::deserializeEntity(j, sceneIn, resources);
    ASSERT_NE(loaded, nullptr);

    std::vector<const Entity*> after;
    collectMeshed(*loaded, after);
    ASSERT_EQ(after.size(), before.size()) << "saved JSON: " << j.dump();

    for (size_t i = 0; i < before.size(); ++i)
    {
        SCOPED_TRACE(before[i]->getName());
        const Mesh& a = *before[i]->getComponent<MeshRenderer>()->getMesh();
        const Mesh& b = *after[i]->getComponent<MeshRenderer>()->getMesh();
        EXPECT_EQ(a.getIndexCount(), b.getIndexCount());
        EXPECT_EQ(a.getLocalBounds().min, b.getLocalBounds().min);
        EXPECT_EQ(a.getLocalBounds().max, b.getLocalBounds().max);
    }
}

// The newest entity a tool placed: the last child of the scene root.
const Entity& lastPlaced(Scene& scene)
{
    const auto& children = scene.getRoot()->getChildren();
    EXPECT_FALSE(children.empty());
    return *children.back();
}

}  // namespace

// ---------------------------------------------------------------------------
// INV-1: every builder's mesh survives a save and load
// ---------------------------------------------------------------------------

TEST_F(ProceduralMeshSaveTest, FactoryWallKeepsItsMeshThroughSaveAndLoad)
{
    ResourceManager resources;
    Scene scene;
    Entity* wall = EntityFactory::createWallWithDoor(scene, resources, glm::vec3(0.0f));
    ASSERT_NE(wall, nullptr);
    expectSurvivesSaveAndLoad(*wall, resources);
}

TEST_F(ProceduralMeshSaveTest, EveryFactoryPresetSurvivesSaveAndLoad)
{
    using Make = std::function<Entity*(Scene&, ResourceManager&, const glm::vec3&)>;
    const std::vector<std::pair<std::string, Make>> presets = {
        {"wall", EntityFactory::createWall},
        {"wall with window", EntityFactory::createWallWithWindow},
        {"room", EntityFactory::createRoom},
        {"roof", EntityFactory::createRoof},
        {"stairs", EntityFactory::createStairs},
        {"spiral stairs", EntityFactory::createSpiralStairs},
        {"floor slab", EntityFactory::createFloorSlab},
    };
    for (const auto& [name, make] : presets)
    {
        SCOPED_TRACE(name);
        ResourceManager resources;
        Scene scene;
        Entity* e = make(scene, resources, glm::vec3(1.0f, 0.0f, 2.0f));
        ASSERT_NE(e, nullptr);
        expectSurvivesSaveAndLoad(*e, resources);
    }
}

TEST_F(ProceduralMeshSaveTest, WallToolWallSurvivesSaveAndLoad)
{
    ResourceManager resources;
    Scene scene;
    CommandHistory history;
    WallTool tool;
    tool.activate();
    ASSERT_TRUE(tool.processClick(glm::vec3(0.0f), scene, resources, history));
    ASSERT_TRUE(tool.processClick(glm::vec3(4.0f, 0.0f, 1.0f), scene, resources, history));
    expectSurvivesSaveAndLoad(lastPlaced(scene), resources);
}

TEST_F(ProceduralMeshSaveTest, RoomToolRoomSurvivesSaveAndLoad)
{
    ResourceManager resources;
    Scene scene;
    CommandHistory history;
    RoomTool tool;
    tool.includeCeiling = true;
    tool.activateClickMode();
    ASSERT_TRUE(tool.processClick(glm::vec3(0.0f), scene, resources, history));
    expectSurvivesSaveAndLoad(lastPlaced(scene), resources);
}

TEST_F(ProceduralMeshSaveTest, RoofToolRoofSurvivesSaveAndLoad)
{
    ResourceManager resources;
    Scene scene;
    CommandHistory history;
    RoofTool tool;
    tool.roofType = RoofType::SHED;
    tool.activate();
    tool.beginPlacing();
    ASSERT_TRUE(tool.processClick(glm::vec3(0.0f), scene, resources, history));
    expectSurvivesSaveAndLoad(lastPlaced(scene), resources);
}

TEST_F(ProceduralMeshSaveTest, StairToolStairsSurviveSaveAndLoad)
{
    for (StairType type : {StairType::STRAIGHT, StairType::SPIRAL})
    {
        SCOPED_TRACE(type == StairType::STRAIGHT ? "straight" : "spiral");
        ResourceManager resources;
        Scene scene;
        CommandHistory history;
        StairTool tool;
        tool.stairType = type;
        tool.activate();
        tool.beginPlacing();
        ASSERT_TRUE(tool.processClick(glm::vec3(0.0f), scene, resources, history));
        expectSurvivesSaveAndLoad(lastPlaced(scene), resources);
    }
}

TEST_F(ProceduralMeshSaveTest, PathToolPathSurvivesSaveAndLoad)
{
    ResourceManager resources;
    Scene scene;
    CommandHistory history;
    PathTool tool;
    tool.activate();
    ASSERT_TRUE(tool.processClick(glm::vec3(0.0f)));
    ASSERT_TRUE(tool.processClick(glm::vec3(5.0f, 0.0f, 2.0f)));
    ASSERT_TRUE(tool.processClick(glm::vec3(9.0f, 0.5f, -1.0f)));
    ASSERT_TRUE(tool.finishPath(scene, resources, history));
    expectSurvivesSaveAndLoad(lastPlaced(scene), resources);
}

// ---------------------------------------------------------------------------
// INV-2: a second cut keeps the first opening; and only a wall can be cut
// ---------------------------------------------------------------------------

TEST_F(ProceduralMeshSaveTest, SecondCutKeepsTheFirstOpening)
{
    ResourceManager resources;
    Scene scene;
    CommandHistory history;
    Entity* wall = EntityFactory::createWall(scene, resources, glm::vec3(0.0f));
    ASSERT_NE(wall, nullptr);

    CutoutTool tool;
    tool.activate();
    tool.selectWall(wall->getId(), scene);
    ASSERT_EQ(tool.getState(), CutoutTool::State::CONFIGURE);
    tool.xPosition = 0.0f;
    tool.applyOpening(scene, history);
    tool.xPosition = 1.0f;
    tool.applyOpening(scene, history);

    const Mesh& cut = *wall->getComponent<MeshRenderer>()->getMesh();
    const MeshRecipe* recipe = cut.getRecipe();
    ASSERT_NE(recipe, nullptr);
    ASSERT_EQ(recipe->openings.size(), 2u);

    const Mesh direct = ProceduralMeshBuilder::createWallWithOpenings(
        recipe->width, recipe->height, recipe->thickness, recipe->openings);

    const nlohmann::json j = EntitySerializer::serializeEntity(*wall, resources);
    Scene sceneIn;
    Entity* loaded = EntitySerializer::deserializeEntity(j, sceneIn, resources);
    ASSERT_NE(loaded, nullptr);
    const auto* mr = loaded->getComponent<MeshRenderer>();
    ASSERT_NE(mr, nullptr) << "saved JSON: " << j.dump();
    EXPECT_EQ(mr->getMesh()->getIndexCount(), direct.getIndexCount());
}

TEST_F(ProceduralMeshSaveTest, CutoutRefusesAMeshThatIsNotAWall)
{
    ResourceManager resources;
    Scene scene;
    Entity* cube = EntityFactory::createCube(scene, resources, glm::vec3(0.0f));
    ASSERT_NE(cube, nullptr);

    CutoutTool tool;
    tool.activate();
    tool.selectWall(cube->getId(), scene);
    EXPECT_EQ(tool.getState(), CutoutTool::State::SELECT_WALL);
}

// ---------------------------------------------------------------------------
// INV-4 and INV-5: a key wins over a recipe; a refused recipe loads no mesh
// ---------------------------------------------------------------------------

TEST_F(ProceduralMeshSaveTest, KeyedMeshSavesKeyNotRecipe)
{
    ResourceManager resources;
    Scene scene;
    Entity* e = scene.createEntity("Cube");
    auto cube = resources.getCubeMesh();
    auto recipe = std::make_shared<MeshRecipe>();
    recipe->width = 3.0f;
    recipe->height = 3.0f;
    recipe->thickness = 0.3f;
    cube->setRecipe(recipe);
    e->addComponent<MeshRenderer>(cube, nullptr);

    const nlohmann::json j = EntitySerializer::serializeEntity(*e, resources);
    const nlohmann::json& mr = j.at("components").at("MeshRenderer");
    EXPECT_EQ(mr.value("mesh", ""), "__builtin_cube");
    EXPECT_FALSE(mr.contains("procedural"));
}

TEST_F(ProceduralMeshSaveTest, RefusedRecipeLoadsWithoutMeshRenderer)
{
    ResourceManager resources;
    Scene scene;
    Entity* wall = EntityFactory::createWall(scene, resources, glm::vec3(0.0f));
    ASSERT_NE(wall, nullptr);

    nlohmann::json j = EntitySerializer::serializeEntity(*wall, resources);
    nlohmann::json& mr = j.at("components").at("MeshRenderer");
    ASSERT_TRUE(mr.contains("procedural")) << j.dump();
    mr["procedural"]["width"] = 5000.0;  // over the 1000 m limit

    Scene sceneIn;
    Entity* loaded = EntitySerializer::deserializeEntity(j, sceneIn, resources);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->getComponent<MeshRenderer>(), nullptr);
}

// ---------------------------------------------------------------------------
// Lane-16 path_tool lead: a vertical stretch of path made NaN vertices
// ---------------------------------------------------------------------------

TEST(ProceduralPath, VerticalTangentGivesFiniteEdges)
{
    const float width = 2.0f;
    const ProceduralMeshData data = ProceduralMeshBuilder::generatePath(
        {glm::vec3(0.0f), glm::vec3(0.0f, 5.0f, 0.0f)}, width, 4);
    ASSERT_GE(data.vertices.size(), 4u);
    for (size_t i = 0; i + 1 < data.vertices.size(); i += 2)
    {
        SCOPED_TRACE("sample " + std::to_string(i / 2));
        const glm::vec3 l = data.vertices[i].position;
        const glm::vec3 r = data.vertices[i + 1].position;
        ASSERT_TRUE(std::isfinite(l.x) && std::isfinite(l.y) && std::isfinite(l.z));
        ASSERT_TRUE(std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.z));
        EXPECT_NEAR(glm::length(r - l), width, 1e-4f);
    }
}

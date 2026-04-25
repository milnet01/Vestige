// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gltf_bounds_checks.cpp
/// @brief Phase 10.9 Slice 5 D10 — pin glTF loader bounds checks for
///        node-children indices and scene-root-node indices.
///
/// The loader takes ownership of out-of-range indices defensively: it
/// drops them with a `Logger::warning` rather than storing them and
/// letting downstream traversal walk off the end of `m_nodes`.
///
/// Tests drive minimal glTF files (asset + scenes + nodes only — no
/// meshes or images) so `ResourceManager::loadTexture` and `Mesh::upload`
/// are not exercised; this lets the test run on the headless CI runner
/// without an OpenGL context.

#include <gtest/gtest.h>
#include "utils/gltf_loader.h"
#include "resource/resource_manager.h"

#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#define VESTIGE_GETPID() _getpid()
#else
#include <unistd.h>
#define VESTIGE_GETPID() getpid()
#endif

namespace fs = std::filesystem;

namespace Vestige::GltfBoundsChecks::Test
{

class GltfBoundsChecksTest : public ::testing::Test
{
protected:
    fs::path m_root;
    fs::path m_gltfPath;

    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const std::string testName = info ? info->name() : "unknown";
        m_root = fs::temp_directory_path()
               / ("vestige_gltf_bounds_test_"
                  + std::to_string(VESTIGE_GETPID()) + "_" + testName);
        std::error_code ec;
        fs::remove_all(m_root, ec);
        fs::create_directories(m_root);
        m_gltfPath = m_root / "test.gltf";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }

    void writeGltf(const std::string& json)
    {
        std::ofstream{m_gltfPath} << json;
    }
};

TEST_F(GltfBoundsChecksTest, ValidGraphLoadsCleanly_D10)
{
    // Control: 2 nodes, scene roots = [0], node 0 has child 1.
    writeGltf(R"({
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "root", "children": [1]},
            {"name": "child"}
        ]
    })");

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->getNodeCount(), 2u);
    ASSERT_EQ(model->m_rootNodes.size(), 1u);
    EXPECT_EQ(model->m_rootNodes[0], 0);
    ASSERT_EQ(model->m_nodes[0].childIndices.size(), 1u);
    EXPECT_EQ(model->m_nodes[0].childIndices[0], 1);
}

TEST_F(GltfBoundsChecksTest, OutOfRangeChildIndexIsDropped_D10)
{
    // Node 0 references child 99 — only 1 node exists.
    // Pre-D10 the loader stored 99; downstream traversal would index
    // m_nodes[99]. Post-D10 the loader drops + warns.
    writeGltf(R"({
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "lone", "children": [99]}
        ]
    })");

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->getNodeCount(), 1u);
    EXPECT_TRUE(model->m_nodes[0].childIndices.empty());
}

TEST_F(GltfBoundsChecksTest, NegativeChildIndexIsDropped_D10)
{
    writeGltf(R"({
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "lone", "children": [-1]}
        ]
    })");

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    ASSERT_NE(model, nullptr);
    EXPECT_TRUE(model->m_nodes[0].childIndices.empty());
}

TEST_F(GltfBoundsChecksTest, OutOfRangeRootNodeIndexIsDropped_D10)
{
    // scene.nodes references node 99 alongside valid 0.
    writeGltf(R"({
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0, 99]}],
        "nodes": [
            {"name": "valid"}
        ]
    })");

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->getNodeCount(), 1u);
    ASSERT_EQ(model->m_rootNodes.size(), 1u);
    EXPECT_EQ(model->m_rootNodes[0], 0);
}

TEST_F(GltfBoundsChecksTest, NegativeRootNodeIndexIsDropped_D10)
{
    writeGltf(R"({
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [-5, 0]}],
        "nodes": [
            {"name": "valid"}
        ]
    })");

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    ASSERT_NE(model, nullptr);
    ASSERT_EQ(model->m_rootNodes.size(), 1u);
    EXPECT_EQ(model->m_rootNodes[0], 0);
}

TEST_F(GltfBoundsChecksTest, OutOfRangeDefaultSceneFallsBackToZero_D10)
{
    // scene = 7 but only one scene exists. Loader falls back to scene 0
    // (existing behaviour, pinned here so the bounds-check helpers don't
    // accidentally regress it).
    writeGltf(R"({
        "asset": {"version": "2.0"},
        "scene": 7,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "valid"}
        ]
    })");

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    ASSERT_NE(model, nullptr);
    ASSERT_EQ(model->m_rootNodes.size(), 1u);
    EXPECT_EQ(model->m_rootNodes[0], 0);
}

}  // namespace Vestige::GltfBoundsChecks::Test

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

#include "gl_test_fixture.h"
#include "test_helpers.h"

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
        m_root = fs::temp_directory_path()
               / ("vestige_gltf_bounds_test_" + Testing::vestigeTestStamp());
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

// -- Phase 10.9 Slice 18 Ts2 — D12 extensionsRequired allowlist pin.
//
// Vestige's loader implements zero glTF extensions (tinygltf parses
// some on the parse side, but our material/mesh paths ignore extension
// data entirely). Per glTF 2.0 §3.12, a file declaring a required
// extension our loader doesn't support must be REJECTED, not silently
// rendered with missing data. The kSupportedRequiredExtensions
// allowlist in `engine/utils/gltf_loader.cpp:1594` is deliberately
// empty, so every required extension is unknown.

TEST_F(GltfBoundsChecksTest, RequiredExtensionUnknownToLoaderIsRejected_D12)
{
    writeGltf(R"({
        "asset": {"version": "2.0"},
        "extensionsRequired": ["KHR_pretend_unsupported"],
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "root"}
        ]
    })");

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    EXPECT_EQ(model, nullptr)
        << "loader must reject required extensions not on its allowlist";
}

TEST_F(GltfBoundsChecksTest, FileWithoutRequiredExtensionsLoads_D12)
{
    // Positive control: no extensionsRequired → loader proceeds.
    writeGltf(R"({
        "asset": {"version": "2.0"},
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "root"}
        ]
    })");

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    EXPECT_NE(model, nullptr);
}

// -- Accessor regions. The offsets and the count come straight from the file, so
// sums and products over them can wrap and pass a bounds check. A rejected
// POSITION accessor skips the primitive before any GL upload, so these need no
// context. The buffer is twelve zero bytes.

static std::string gltfWithPositionAccessor(const std::string& bufferViewFields,
                                            const std::string& accessorFields)
{
    return R"({
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": 12,
                     "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAA"}],
        "bufferViews": [{"buffer": 0, "byteLength": 12, )" + bufferViewFields + R"(}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "type": "VEC3", )"
        + accessorFields + R"(}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
        "nodes": [{"mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0
    })";
}

TEST_F(GltfBoundsChecksTest, AccessorOffsetSumThatWrapsIsRejected)
{
    // 8 + (2^64 - 8) wraps to 0, which looks like the start of the buffer.
    writeGltf(gltfWithPositionAccessor(
        R"("byteOffset": 8)", R"("byteOffset": 18446744073709551608, "count": 1)"));

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    ASSERT_NE(model, nullptr);
    EXPECT_TRUE(model->m_primitives.empty());
}

TEST_F(GltfBoundsChecksTest, AccessorCountThatWrapsTheSizeIsRejected)
{
    // (2^60 + 1 - 1) * 16 wraps to 0, so the region appears to be one element.
    writeGltf(gltfWithPositionAccessor(
        R"("byteOffset": 0, "byteStride": 16)", R"("count": 1152921504606846977)"));

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    ASSERT_NE(model, nullptr);
    EXPECT_TRUE(model->m_primitives.empty());
}

TEST_F(GltfBoundsChecksTest, BufferViewOffsetPastTheBufferIsRejected)
{
    // The view's offset is checked alone first: subtracting it from the buffer
    // size would otherwise wrap to a huge "remaining" size.
    writeGltf(gltfWithPositionAccessor(R"("byteOffset": 16)", R"("byteOffset": 0, "count": 1)"));

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    ASSERT_NE(model, nullptr);
    EXPECT_TRUE(model->m_primitives.empty());
}

TEST_F(GltfBoundsChecksTest, AccessorOneElementPastTheBufferIsRejected)
{
    writeGltf(gltfWithPositionAccessor(R"("byteOffset": 0)", R"("byteOffset": 4, "count": 1)"));

    ResourceManager rm;
    auto model = GltfLoader::load(m_gltfPath.string(), rm);
    ASSERT_NE(model, nullptr);
    EXPECT_TRUE(model->m_primitives.empty());
}

// -- A skin attribute may name an accessor that does not exist. The loader must
// check the index before it reads the accessor to pick an element size.

static std::string gltfWithSkinAttribute(const std::string& attribute)
{
    return R"({
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": 12,
                     "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAA"}],
        "bufferViews": [{"buffer": 0, "byteLength": 12}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "type": "VEC3", "count": 1}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, ")" + attribute + R"(": 99}}]}],
        "nodes": [{"mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0
    })";
}

class GltfSkinIndexTest : public ::Vestige::Test::GLTestFixture
{
protected:
    fs::path m_path;

    void SetUp() override
    {
        GLTestFixture::SetUp();
        if (IsSkipped()) { return; }
        m_path = fs::temp_directory_path()
               / ("vestige_gltf_skin_" + Testing::vestigeTestStamp() + ".gltf");
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove(m_path, ec);
    }
};

TEST_F(GltfSkinIndexTest, JointsAccessorIndexOutOfRangeIsIgnored)
{
    std::ofstream{m_path} << gltfWithSkinAttribute("JOINTS_0");
    ResourceManager rm;
    auto model = GltfLoader::load(m_path.string(), rm);
    EXPECT_NE(model, nullptr);
}

TEST_F(GltfSkinIndexTest, WeightsAccessorIndexOutOfRangeIsIgnored)
{
    std::ofstream{m_path} << gltfWithSkinAttribute("WEIGHTS_0");
    ResourceManager rm;
    auto model = GltfLoader::load(m_path.string(), rm);
    EXPECT_NE(model, nullptr);
}

}  // namespace Vestige::GltfBoundsChecks::Test

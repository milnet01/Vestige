// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_acoustic_baker.cpp
/// @brief AX3 B3 — bake driver + sidecar artifact. Covers the pure pieces
///        (coplanar-merge, AABB volume, geometry fingerprint), the real Jolt
///        triangle decode from a static body, and the design's B3 verify:
///        "bake a test scene; artifact round-trips; re-bake is deterministic."

#include "audio/acoustic_baker.h"

#include "audio/acoustic_bake.h"
#include "audio/audio_clip.h"
#include "core/job_system.h"
#include "physics/physics_world.h"
#include "physics/surface_material.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include <nlohmann/json.hpp>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

using namespace Vestige;
namespace fs = std::filesystem;

namespace
{

/// A quad (two triangles) on the plane z = `z`, spanning [-1,1]² in x/y.
/// `flip` reverses the winding so the two triangles carry opposite normals —
/// proving the merge treats n and -n as the same plane.
void pushQuadAtZ(std::vector<BakeTriangle>& out, float z, SurfaceMaterial mat, bool flip)
{
    const glm::vec3 a(-1.0f, -1.0f, z), b(1.0f, -1.0f, z), c(1.0f, 1.0f, z), d(-1.0f, 1.0f, z);
    if (flip)
    {
        out.push_back({ a, c, b, mat });
        out.push_back({ a, d, c, mat });
    }
    else
    {
        out.push_back({ a, b, c, mat });
        out.push_back({ a, c, d, mat });
    }
}

float totalArea(const std::vector<ReflectingFacet>& facets)
{
    return std::accumulate(facets.begin(), facets.end(), 0.0f,
                           [](float acc, const ReflectingFacet& f) { return acc + f.area; });
}

std::vector<char> readFileBytes(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

} // namespace

// --- Pure: coplanar merge ---------------------------------------------------

TEST(AcousticBaker, MergeSumsCoplanarAreaAndKeepsParallelWallsDistinct)
{
    std::vector<BakeTriangle> tris;
    pushQuadAtZ(tris, 0.0f, SurfaceMaterial::Stone, /*flip=*/false);
    pushQuadAtZ(tris, 0.0f, SurfaceMaterial::Stone, /*flip=*/true);   // opposite winding, same plane
    pushQuadAtZ(tris, 1.0f, SurfaceMaterial::Stone, /*flip=*/false);  // parallel wall 1 m away

    const auto facets = mergeTrianglesToFacets(tris, 1.0f);

    // z=0 (both windings) folds into one facet; z=1 is a distinct wall.
    ASSERT_EQ(facets.size(), 2u);
    EXPECT_FLOAT_EQ(facets[0].area, 8.0f);  // two 2×2 quads = 4 + 4
    EXPECT_FLOAT_EQ(facets[1].area, 4.0f);  // one 2×2 quad
}

TEST(AcousticBaker, MergeSplitsDifferentMaterialsOnSamePlane)
{
    // Coplanar quads of different materials must NOT merge — α differs.
    std::vector<BakeTriangle> tris;
    pushQuadAtZ(tris, 0.0f, SurfaceMaterial::Stone, false);
    pushQuadAtZ(tris, 0.0f, SurfaceMaterial::Cloth, false);

    const auto facets = mergeTrianglesToFacets(tris, 1.0f);
    ASSERT_EQ(facets.size(), 2u);
}

// --- Pure: AABB volume ------------------------------------------------------

TEST(AcousticBaker, AabbVolumeSpansTriangleExtent)
{
    std::vector<BakeTriangle> tris;
    pushQuadAtZ(tris, 0.0f, SurfaceMaterial::Stone, false);  // spans x,y ∈ [-1,1]
    pushQuadAtZ(tris, 3.0f, SurfaceMaterial::Stone, false);  // z ∈ [0,3]
    EXPECT_FLOAT_EQ(trianglesAabbVolume(tris), 2.0f * 2.0f * 3.0f);  // 2×2×3 = 12
    EXPECT_FLOAT_EQ(trianglesAabbVolume({}), 0.0f);
}

// --- Pure: geometry fingerprint ---------------------------------------------

TEST(AcousticBaker, FingerprintIsDeterministicAndSensitive)
{
    const std::vector<ReflectingFacet> facets = {
        { glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), 8.0f, SurfaceMaterial::Stone },
    };
    const std::vector<glm::vec3> probes = { glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f) };

    const std::uint64_t base = geometryFingerprint(facets, probes);
    EXPECT_EQ(base, geometryFingerprint(facets, probes));  // deterministic

    // Move a probe → different fingerprint.
    std::vector<glm::vec3> movedProbes = probes;
    movedProbes[1].x = 2.0f;
    EXPECT_NE(base, geometryFingerprint(facets, movedProbes));

    // Change a facet material → different fingerprint.
    std::vector<ReflectingFacet> clothFacets = facets;
    clothFacets[0].material = SurfaceMaterial::Cloth;
    EXPECT_NE(base, geometryFingerprint(clothFacets, probes));
}

// --- Integration: real Jolt triangle decode from a static body --------------

TEST(AcousticBaker, ExtractsSixCleanFacetsFromStaticBox)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    // Zero convex radius → the decode is exactly the 6 box faces (12 tris),
    // not the bevelled default. Dims 4×4×3 → surface area 2·(16+12+12)=80.
    JPH::BoxShape* shape = new JPH::BoxShape(JPH::Vec3(2.0f, 2.0f, 1.5f), 0.0f);
    const JPH::BodyID id = world.createStaticBody(shape, glm::vec3(0.0f));
    world.setBodyTags(id, 0u, SurfaceMaterial::Stone);

    int untagged = -1;
    const auto tris = extractStaticTriangles(world, { id }, untagged);
    EXPECT_EQ(untagged, 0);              // tagged Stone
    EXPECT_EQ(tris.size(), 12u);         // 6 faces × 2 triangles

    const auto facets = mergeTrianglesToFacets(tris, 1.0f);
    EXPECT_EQ(facets.size(), 6u);
    EXPECT_NEAR(totalArea(facets), 80.0f, 1e-3f);
    for (const ReflectingFacet& f : facets)
    {
        EXPECT_EQ(f.material, SurfaceMaterial::Stone);
    }
    EXPECT_NEAR(trianglesAabbVolume(tris), 48.0f, 1e-3f);  // 4×4×3

    world.shutdown();
}

TEST(AcousticBaker, UntaggedStaticBodyIsCounted)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());
    JPH::BoxShape* shape = new JPH::BoxShape(JPH::Vec3(1.0f, 1.0f, 1.0f), 0.0f);
    const JPH::BodyID id = world.createStaticBody(shape, glm::vec3(0.0f));
    // No setBodyTags → reads back SurfaceMaterial::Default.

    int untagged = -1;
    (void)extractStaticTriangles(world, { id }, untagged);
    EXPECT_EQ(untagged, 1);

    world.shutdown();
}

// --- Integration: B3 verify — bake a scene, round-trip, determinism ---------

TEST(AcousticBaker, BakeAndWriteRoundTripsAndIsDeterministic)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());
    JPH::BoxShape* shape = new JPH::BoxShape(JPH::Vec3(2.0f, 2.0f, 1.5f), 0.0f);
    const JPH::BodyID id = world.createStaticBody(shape, glm::vec3(0.0f));
    world.setBodyTags(id, 0u, SurfaceMaterial::Stone);

    int untagged = 0;
    const auto tris = extractStaticTriangles(world, { id }, untagged);
    const auto facets = mergeTrianglesToFacets(tris, 1.0f);
    const float volume = trianglesAabbVolume(tris);

    const std::vector<BakeProbe> probes = {
        { 1u, glm::vec3(0.5f, 0.3f, 0.2f), 10.0f },
        { 2u, glm::vec3(-0.4f, 0.1f, -0.3f), 8.0f },
    };

    JobSystem jobs(JobSystemConfig{ 0 });  // synchronous → deterministic

    const fs::path root = fs::temp_directory_path() / "vestige_ax3_bake_test";
    fs::remove_all(root);
    const std::string dirA = (root / "a").string();
    const std::string dirB = (root / "b").string();

    const AcousticBakeResult ra = bakeAndWrite(facets, volume, probes, BakeParams{}, jobs, dirA);
    ASSERT_TRUE(ra.ok);
    ASSERT_EQ(ra.probes.size(), 2u);

    // Sidecar files exist.
    EXPECT_TRUE(fs::exists(fs::path(dirA) / "probe_1.wav"));
    EXPECT_TRUE(fs::exists(fs::path(dirA) / "probe_2.wav"));
    EXPECT_TRUE(fs::exists(fs::path(dirA) / "acoustics_index.json"));

    // Index parses and matches. Scope the reader so its handle is released
    // before the `fs::remove_all` below — Windows refuses to delete a file that
    // still has an open handle (a bare function-scope ifstream here would make
    // that cleanup fail with a sharing violation).
    {
        std::ifstream idxIn((fs::path(dirA) / "acoustics_index.json").string());
        nlohmann::json idx;
        idxIn >> idx;
        EXPECT_EQ(idx["version"].get<int>(), 1);
        EXPECT_EQ(idx["sampleRate"].get<int>(), BakeParams{}.sampleRate);
        EXPECT_EQ(idx["geometryFingerprint"].get<std::uint64_t>(), ra.geometryFingerprint);
        ASSERT_EQ(idx["probes"].size(), 2u);
        EXPECT_EQ(idx["probes"][0]["ir"].get<std::string>(), "probe_1.wav");
    }

    // Each IR round-trips through the runtime loader (float-WAV → s16).
    const auto clip = AudioClip::loadFromFile((fs::path(dirA) / "probe_1.wav").string());
    ASSERT_TRUE(clip.has_value());
    EXPECT_EQ(clip->getChannels(), 1u);
    EXPECT_EQ(clip->getSampleRate(), static_cast<uint32_t>(BakeParams{}.sampleRate));
    EXPECT_GT(clip->getFrameCount(), 0u);

    // Re-bake to a second dir → identical fingerprint + byte-identical IRs.
    const AcousticBakeResult rb = bakeAndWrite(facets, volume, probes, BakeParams{}, jobs, dirB);
    ASSERT_TRUE(rb.ok);
    EXPECT_EQ(ra.geometryFingerprint, rb.geometryFingerprint);
    EXPECT_EQ(readFileBytes((fs::path(dirA) / "probe_1.wav").string()),
              readFileBytes((fs::path(dirB) / "probe_1.wav").string()));
    EXPECT_EQ(readFileBytes((fs::path(dirA) / "probe_2.wav").string()),
              readFileBytes((fs::path(dirB) / "probe_2.wav").string()));
    fs::remove_all(root);
    world.shutdown();
}

TEST(AcousticBaker, EmptyProbeSetWritesNothingButFingerprints)
{
    JobSystem jobs(JobSystemConfig{ 0 });
    const std::vector<ReflectingFacet> facets = {
        { glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), 8.0f, SurfaceMaterial::Stone },
    };
    const fs::path dir = fs::temp_directory_path() / "vestige_ax3_empty_test";
    fs::remove_all(dir);

    const AcousticBakeResult r = bakeAndWrite(facets, 48.0f, {}, BakeParams{}, jobs, dir.string());
    EXPECT_FALSE(r.ok);                       // no probes → not a successful bake
    EXPECT_NE(r.geometryFingerprint, 0u);     // fingerprint still computed
    EXPECT_FALSE(fs::exists(dir / "acoustics_index.json"));  // no dir/files created

    fs::remove_all(dir);
}

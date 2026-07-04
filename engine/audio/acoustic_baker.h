// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file acoustic_baker.h
/// @brief AX3 B3 — offline acoustic bake driver + sidecar artifact.
///
/// Turns a scene's static physics geometry + acoustic probes into per-probe
/// impulse-response `.wav` sidecars plus an `acoustics_index.json`, so the
/// runtime (B4) can feed the nearest probe's IR to the reverb convolution slot.
///
/// Split into pure/testable pieces (triangle → facet merge, AABB volume,
/// geometry fingerprint, WAV write) and two physics/scene-facing drivers. The
/// heavy per-probe math is the pure `bakeProbeIr` (`acoustic_bake.h`), run
/// across MT2 workers; only facet extraction touches Jolt and must stay on the
/// main thread (`getSurfaceMaterial` / `GetTrianglesStart` take body locks).
///
/// Facet-source deviation from the design (§6.2): the design names each static
/// `RigidBody`'s stored `collisionVertices/Indices` as the *primary* source and
/// Jolt `GetTrianglesStart/Next` as a *fallback* for box/hull bodies. Since box
/// walls (the common architectural case) carry no stored mesh and the Jolt
/// decode covers every shape type uniformly — box, hull, mesh, sphere, capsule —
/// this uses the single Jolt path for all bodies. Simpler, one code path, and
/// exactly the decode the design already specifies; documented per Rule 5.
#pragma once

#include "audio/acoustic_bake.h"
#include "physics/surface_material.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Vestige
{

class Scene;
class PhysicsWorld;
class JobSystem;

/// @brief A world-space triangle tagged with its body's surface material — the
///        raw output of static-geometry extraction, before coplanar merge.
struct BakeTriangle
{
    glm::vec3 v0 = glm::vec3(0.0f);
    glm::vec3 v1 = glm::vec3(0.0f);
    glm::vec3 v2 = glm::vec3(0.0f);
    SurfaceMaterial material = SurfaceMaterial::Default;
};

/// @brief One acoustic probe to bake, as gathered from the scene.
struct BakeProbe
{
    std::uint32_t id = 0;               ///< Owning entity id (sidecar filename key).
    glm::vec3 position = glm::vec3(0.0f);
    float influenceRadius = 10.0f;
};

/// @brief A baked probe as recorded in `acoustics_index.json`.
struct BakedProbeRecord
{
    std::uint32_t id = 0;
    glm::vec3 position = glm::vec3(0.0f);
    float influenceRadius = 10.0f;
    std::string irFile;                 ///< Relative filename, e.g. "probe_7.wav".
};

/// @brief The product of a bake: the merged facets (for debug/inspection), the
///        Sabine volume, the staleness fingerprint, and the written probe
///        records. `ok` is false (with empty vectors) when the scene has no
///        static geometry or no probes — logged, not a hard error.
struct AcousticBakeResult
{
    bool ok = false;
    std::vector<ReflectingFacet> facets;
    float roomVolumeM3 = 0.0f;
    std::uint64_t geometryFingerprint = 0;
    std::vector<BakedProbeRecord> probes;
};

// --- Pure, testable helpers -------------------------------------------------

/// @brief Merge coplanar, same-material triangles into `ReflectingFacet`s,
///        summing their areas (the `Sᵢ` Sabine needs). Greedy clustering: a
///        triangle joins an existing facet whose plane matches within
///        `mergeToleranceDeg` (normal angle) and a small offset epsilon, else
///        starts a new facet. Plane orientation is irrelevant to the
///        image-source reflection, so `n` and `-n` are treated as the same
///        plane. Degenerate (zero-area) triangles are dropped.
std::vector<ReflectingFacet> mergeTrianglesToFacets(const std::vector<BakeTriangle>& tris,
                                                    float mergeToleranceDeg);

/// @brief Axis-aligned bounding-box volume of a triangle soup — the enclosing
///        volume `V` Sabine's equation uses (design §6.2). Zero for < 1 tri.
float trianglesAabbVolume(const std::vector<BakeTriangle>& tris);

/// @brief Deterministic 64-bit fingerprint of the baked geometry + probe set,
///        so an unchanged scene re-bake can be skipped (§6.4). Order-sensitive
///        by construction (facets/probes come from a stable scene walk).
std::uint64_t geometryFingerprint(const std::vector<ReflectingFacet>& facets,
                                  const std::vector<glm::vec3>& probePositions);

/// @brief Write a mono impulse response as a 32-bit IEEE-float WAV (design
///        §6.2 step 3: baker writes float-in-WAV; runtime `AudioClip` decodes
///        to s16). Returns false on I/O error (logged).
bool writeIrWav(const std::string& path, const std::vector<float>& ir, int sampleRate);

// --- Physics/scene-facing drivers (MAIN THREAD) -----------------------------

/// @brief Decode world-space triangles from a set of static Jolt bodies via
///        `Shape::GetTrianglesStart/Next`, tagging each with the body's
///        `SurfaceMaterial` (`PhysicsWorld::getSurfaceMaterial`). MAIN-THREAD
///        ONLY (body locks). `outUntaggedCount` receives the number of bodies
///        that read back `SurfaceMaterial::Default` (the caller warns).
std::vector<BakeTriangle> extractStaticTriangles(const PhysicsWorld& world,
                                                 const std::vector<JPH::BodyID>& bodies,
                                                 int& outUntaggedCount);

/// @brief Bake every probe (MT2 `parallelFor` over the pure `bakeProbeIr`) and
///        write `<outputDir>/probe_<id>.wav` + `<outputDir>/acoustics_index.json`.
///        `facets`/`roomVolumeM3` are the already-extracted geometry; probes are
///        independent so they parallelise cleanly. Returns the populated result
///        (`ok` false + no files written if `probes` is empty).
AcousticBakeResult bakeAndWrite(const std::vector<ReflectingFacet>& facets,
                                float roomVolumeM3,
                                const std::vector<BakeProbe>& probes,
                                const BakeParams& params,
                                JobSystem& jobs,
                                const std::string& outputDir);

/// @brief Full driver: walk `scene` for static `RigidBody`s and
///        `AcousticProbeComponent`s, extract facets (main thread), then
///        `bakeAndWrite` into `outputDir` (`<scene>_acoustics/`). This is what
///        the editor "Bake Acoustics" button / headless CLI (B5) calls.
AcousticBakeResult bakeScene(Scene& scene,
                             PhysicsWorld& world,
                             JobSystem& jobs,
                             const std::string& outputDir,
                             const BakeParams& params);

} // namespace Vestige

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file acoustic_baker.cpp
/// @brief AX3 B3 — offline acoustic bake driver + sidecar artifact.

#include "audio/acoustic_baker.h"

#include "audio/acoustic_probe_component.h"
#include "core/job_system.h"
#include "core/logger.h"
#include "physics/physics_world.h"
#include "physics/rigid_body.h"
#include "scene/entity.h"
#include "scene/scene.h"

#include <Jolt/Jolt.h>
#include <Jolt/Geometry/AABox.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <dr_wav.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace Vestige
{

namespace
{

using json = nlohmann::json;
namespace fs = std::filesystem;

/// FNV-1a 64-bit accumulator over raw object bytes (reading an object's
/// representation through `unsigned char*` is well-defined and warning-free).
struct Fnv1a
{
    std::uint64_t h = 1469598103934665603ULL;
    void mix(const void* data, std::size_t n)
    {
        const auto* p = reinterpret_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i)
        {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
    }
    void mixFloat(float f)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        mix(&bits, sizeof(bits));
    }
};

/// The plane of a triangle as `(n, d)` with `n` unit, plus its area. `valid`
/// is false for a degenerate (zero-area) triangle.
struct TrianglePlane
{
    glm::vec4 plane = glm::vec4(0.0f);
    float area = 0.0f;
    bool valid = false;
};

TrianglePlane planeOfTriangle(const BakeTriangle& t)
{
    const glm::vec3 cross = glm::cross(t.v1 - t.v0, t.v2 - t.v0);
    const float len = glm::length(cross);
    if (len < 1e-8f)
    {
        return {};  // degenerate sliver — no reflecting surface
    }
    const glm::vec3 n = cross / len;
    return { glm::vec4(n, -glm::dot(n, t.v0)), 0.5f * len, true };
}

/// Write the probe index JSON next to the IR sidecars.
bool writeAcousticsIndex(const std::string& outputDir, int sampleRate, float roomVolumeM3,
                         std::uint64_t fingerprint, const std::vector<BakedProbeRecord>& records)
{
    json j;
    j["version"] = 1;
    j["sampleRate"] = sampleRate;
    j["roomVolumeM3"] = roomVolumeM3;
    j["geometryFingerprint"] = fingerprint;

    json arr = json::array();
    for (const BakedProbeRecord& r : records)
    {
        json p;
        p["id"] = r.id;
        p["position"] = { r.position.x, r.position.y, r.position.z };
        p["influenceRadius"] = r.influenceRadius;
        p["ir"] = r.irFile;
        arr.push_back(std::move(p));
    }
    j["probes"] = std::move(arr);

    const std::string path = (fs::path(outputDir) / "acoustics_index.json").string();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        Logger::error("[AcousticBaker] cannot open index for write: " + path);
        return false;
    }
    out << j.dump(2) << '\n';
    if (out.fail())
    {
        Logger::error("[AcousticBaker] failed writing index: " + path);
        return false;
    }
    return true;
}

} // namespace

std::vector<ReflectingFacet> mergeTrianglesToFacets(const std::vector<BakeTriangle>& tris,
                                                    float mergeToleranceDeg)
{
    // Two planes are "the same" if their normals are aligned (or anti-aligned —
    // reflection is orientation-free) within the angle tolerance and their
    // offsets coincide within a small absolute epsilon.
    const float cosTol = std::cos(glm::radians(std::max(mergeToleranceDeg, 0.0f)));
    constexpr float kOffsetEps = 0.02f;  // metres — parallel walls stay distinct

    std::vector<ReflectingFacet> facets;
    for (const BakeTriangle& t : tris)
    {
        const TrianglePlane tp = planeOfTriangle(t);
        if (!tp.valid)
        {
            continue;
        }
        const glm::vec3 n(tp.plane);
        const float d = tp.plane.w;

        bool merged = false;
        for (ReflectingFacet& f : facets)
        {
            if (f.material != t.material)
            {
                continue;
            }
            const glm::vec3 fn(f.plane);
            const float dot = glm::dot(fn, n);
            if (std::abs(dot) < cosTol)
            {
                continue;  // normals too far apart
            }
            // Align this triangle's (n,d) to the facet's orientation before
            // comparing offsets (n and -n describe the same plane).
            const float alignedD = dot < 0.0f ? -d : d;
            if (std::abs(f.plane.w - alignedD) > kOffsetEps)
            {
                continue;  // parallel but offset — a different wall
            }
            f.area += tp.area;  // same surface — accumulate Sᵢ
            merged = true;
            break;
        }
        if (!merged)
        {
            facets.push_back({ tp.plane, tp.area, t.material });
        }
    }
    return facets;
}

float trianglesAabbVolume(const std::vector<BakeTriangle>& tris)
{
    if (tris.empty())
    {
        return 0.0f;
    }
    glm::vec3 lo = tris[0].v0;
    glm::vec3 hi = tris[0].v0;
    for (const BakeTriangle& t : tris)
    {
        for (const glm::vec3& v : { t.v0, t.v1, t.v2 })
        {
            lo = glm::min(lo, v);
            hi = glm::max(hi, v);
        }
    }
    const glm::vec3 ext = hi - lo;
    return ext.x * ext.y * ext.z;
}

std::uint64_t geometryFingerprint(const std::vector<ReflectingFacet>& facets,
                                  const std::vector<glm::vec3>& probePositions)
{
    Fnv1a fnv;
    for (const ReflectingFacet& f : facets)
    {
        fnv.mixFloat(f.plane.x);
        fnv.mixFloat(f.plane.y);
        fnv.mixFloat(f.plane.z);
        fnv.mixFloat(f.plane.w);
        fnv.mixFloat(f.area);
        const auto mat = static_cast<std::uint8_t>(f.material);
        fnv.mix(&mat, sizeof(mat));
    }
    for (const glm::vec3& p : probePositions)
    {
        fnv.mixFloat(p.x);
        fnv.mixFloat(p.y);
        fnv.mixFloat(p.z);
    }
    return fnv.h;
}

bool writeIrWav(const std::string& path, const std::vector<float>& ir, int sampleRate)
{
    drwav_data_format fmt{};
    fmt.container = drwav_container_riff;
    fmt.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels = 1;
    fmt.sampleRate = static_cast<drwav_uint32>(std::max(sampleRate, 1));
    fmt.bitsPerSample = 32;

    drwav wav;
    if (!drwav_init_file_write(&wav, path.c_str(), &fmt, nullptr))
    {
        Logger::error("[AcousticBaker] cannot open IR for write: " + path);
        return false;
    }
    const auto frames = static_cast<drwav_uint64>(ir.size());
    const drwav_uint64 written = drwav_write_pcm_frames(&wav, frames, ir.data());
    drwav_uninit(&wav);
    if (written != frames)
    {
        Logger::error("[AcousticBaker] IR partial write (" + std::to_string(written) +
                      "/" + std::to_string(frames) + " frames): " + path);
        return false;
    }
    return true;
}

std::vector<BakeTriangle> extractStaticTriangles(const PhysicsWorld& world,
                                                 const std::vector<JPH::BodyID>& bodies,
                                                 int& outUntaggedCount)
{
    outUntaggedCount = 0;
    std::vector<BakeTriangle> tris;

    const JPH::BodyInterface& bi = world.getBodyInterface();
    for (const JPH::BodyID id : bodies)
    {
        if (id.IsInvalid())
        {
            continue;
        }
        const JPH::RefConst<JPH::Shape> shape = bi.GetShape(id);
        if (shape.GetPtr() == nullptr)
        {
            continue;
        }
        const SurfaceMaterial mat = world.getSurfaceMaterial(id);
        if (mat == SurfaceMaterial::Default)
        {
            ++outUntaggedCount;
        }

        // Jolt decodes triangles into world space given the body's COM
        // transform (unit scale — the shape already carries its own extents).
        JPH::Shape::GetTrianglesContext ctx;
        shape->GetTrianglesStart(ctx, JPH::AABox::sBiggest(),
                                 bi.GetCenterOfMassPosition(id), bi.GetRotation(id),
                                 JPH::Vec3::sReplicate(1.0f));

        constexpr int kBatch = 256;  // ≥ cGetTrianglesMinTrianglesRequested (32)
        std::array<JPH::Float3, static_cast<std::size_t>(kBatch) * 3> verts{};
        const auto toGlm = [](const JPH::Float3& f) { return glm::vec3(f.x, f.y, f.z); };
        for (;;)
        {
            const int got = shape->GetTrianglesNext(ctx, kBatch, verts.data(), nullptr);
            if (got <= 0)
            {
                break;
            }
            for (int t = 0; t < got; ++t)
            {
                const auto base = static_cast<std::size_t>(t) * 3;
                tris.push_back({ toGlm(verts[base]), toGlm(verts[base + 1]),
                                 toGlm(verts[base + 2]), mat });
            }
        }
    }
    return tris;
}

AcousticBakeResult bakeAndWrite(const std::vector<ReflectingFacet>& facets,
                                float roomVolumeM3,
                                const std::vector<BakeProbe>& probes,
                                const BakeParams& params,
                                JobSystem& jobs,
                                const std::string& outputDir)
{
    AcousticBakeResult result;
    result.facets = facets;
    result.roomVolumeM3 = roomVolumeM3;

    std::vector<glm::vec3> probePositions;
    probePositions.reserve(probes.size());
    for (const BakeProbe& p : probes)
    {
        probePositions.push_back(p.position);
    }
    result.geometryFingerprint = geometryFingerprint(facets, probePositions);

    if (probes.empty())
    {
        Logger::warning("[AcousticBaker] no acoustic probes in scene — nothing to bake");
        return result;  // ok stays false
    }

    std::error_code ec;
    fs::create_directories(outputDir, ec);
    if (ec)
    {
        Logger::error("[AcousticBaker] cannot create output dir '" + outputDir + "': " + ec.message());
        return result;
    }

    // Pure per-probe bake across MT2 workers — probes are independent and each
    // worker writes a disjoint `irs[i]`, so no synchronisation is needed.
    std::vector<std::vector<float>> irs(probes.size());
    const JobHandle handle = jobs.parallelFor(
        static_cast<std::uint32_t>(probes.size()),
        [&](std::uint32_t begin, std::uint32_t end)
        {
            for (std::uint32_t i = begin; i < end; ++i)
            {
                irs[i] = bakeProbeIr(facets, probes[i].position, roomVolumeM3, params);
            }
        });
    jobs.wait(handle);

    const int sampleRate = std::max(params.sampleRate, 1);
    bool allOk = true;
    result.probes.reserve(probes.size());
    for (std::size_t i = 0; i < probes.size(); ++i)
    {
        const std::string fname = "probe_" + std::to_string(probes[i].id) + ".wav";
        const std::string path = (fs::path(outputDir) / fname).string();
        if (!writeIrWav(path, irs[i], sampleRate))
        {
            allOk = false;
            continue;
        }
        result.probes.push_back({ probes[i].id, probes[i].position,
                                  probes[i].influenceRadius, fname });
    }

    if (!writeAcousticsIndex(outputDir, sampleRate, roomVolumeM3,
                             result.geometryFingerprint, result.probes))
    {
        allOk = false;
    }

    result.ok = allOk && !result.probes.empty();
    return result;
}

AcousticBakeResult bakeScene(Scene& scene,
                             PhysicsWorld& world,
                             JobSystem& jobs,
                             const std::string& outputDir,
                             const BakeParams& params)
{
    std::vector<JPH::BodyID> bodies;
    std::vector<BakeProbe> probes;
    scene.forEachEntity(
        [&](Entity& entity)
        {
            if (auto* rb = entity.getComponent<RigidBody>();
                rb != nullptr && rb->motionType == BodyMotionType::STATIC && rb->hasBody())
            {
                bodies.push_back(rb->getBodyId());
            }
            if (auto* probe = entity.getComponent<AcousticProbeComponent>(); probe != nullptr)
            {
                probes.push_back({ entity.getId(), entity.getWorldPosition(), probe->influenceRadius });
            }
        });

    int untagged = 0;
    const std::vector<BakeTriangle> tris = extractStaticTriangles(world, bodies, untagged);
    if (untagged > 0)
    {
        Logger::warning("[AcousticBaker] " + std::to_string(untagged) +
                        " static body(ies) untagged (SurfaceMaterial::Default) — "
                        "tag architecture surfaces for accurate reverb");
    }

    const std::vector<ReflectingFacet> facets =
        mergeTrianglesToFacets(tris, params.coplanarMergeToleranceDeg);
    const float volume = trianglesAabbVolume(tris);
    if (facets.empty())
    {
        Logger::warning("[AcousticBaker] no static geometry found — probes bake open-room IRs");
    }

    return bakeAndWrite(facets, volume, probes, params, jobs, outputDir);
}

} // namespace Vestige

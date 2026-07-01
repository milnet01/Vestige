// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_occlusion_system.cpp
/// @brief AudioOcclusionSystem implementation (AX1 S2 — single-ray skeleton).
#include "systems/audio_occlusion_system.h"

#include "audio/audio_mixer.h"
#include "audio/audio_source_component.h"
#include "audio/occlusion_material_map.h"
#include "core/engine.h"
#include "core/job_system.h"
#include "core/logger.h"
#include "physics/physics_world.h"
#include "physics/rigid_body.h"
#include "scene/component.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_set>

namespace Vestige
{

namespace
{

/// @brief Exponential slew rate for `occlusionFraction`, in units/second, so a
///        source crossing behind a wall muffles over ~125 ms instead of
///        clicking on the frame the ray flips. Hand-tuned — no measured
///        reference exists.
/// TODO: revisit via Formula Workbench once a perceptual target curve exists.
constexpr float kOcclusionSlewPerSec = 8.0f;

/// @brief Rays cast per spatial source. S3 default; becomes the
///        `occlusionRayCount` setting in S5. 8 rays give a smooth open-path
///        fraction near edges at 2× budget headroom (§4.5).
/// TODO: revisit via Formula Workbench once a perceptual target curve exists.
constexpr int kDefaultOcclusionRayCount = 8;

/// @brief Radius (metres) of the sphere the extra rays sample toward, so a
///        source near a doorway edge reads a partial fraction. S3 default;
///        becomes the `occlusionSourceRadius` setting in S5.
constexpr float kDefaultOcclusionSourceRadius = 0.5f;

/// @brief Cull radius (metres): beyond it, occlusion is imperceptible under
///        distance attenuation, so no rays are cast. S4 default; becomes the
///        `occlusionMaxDistance` setting in S5.
constexpr float kDefaultOcclusionMaxDistance = 40.0f;

/// @brief A source whose pre-compose gain (`master × bus × volume`, no
///        occlusion/ducking) is at or below this is effectively silent → not
///        worth casting rays for.
constexpr float kAudibleGainEps = 1e-4f;

} // namespace

const std::array<glm::vec3, kMaxOcclusionRayCount>& occlusionRayOffsets()
{
    static const std::array<glm::vec3, kMaxOcclusionRayCount> table = []
    {
        std::array<glm::vec3, kMaxOcclusionRayCount> t{};
        t[0] = glm::vec3(0.0f);  // ray 0 → exact source centre

        // Fibonacci lattice over the remaining entries: y linearly spaced away
        // from the poles, azimuth advanced by the golden angle. Each is a unit
        // vector, so scaling by the sampling radius places it on the source
        // sphere.
        constexpr int kSpherePoints = kMaxOcclusionRayCount - 1;
        const float goldenAngle =
            glm::pi<float>() * (3.0f - std::sqrt(5.0f));
        for (int i = 0; i < kSpherePoints; ++i)
        {
            const float y =
                1.0f - (2.0f * static_cast<float>(i) + 1.0f)
                           / static_cast<float>(kSpherePoints);
            const float ring   = std::sqrt(std::max(0.0f, 1.0f - y * y));
            const float theta  = goldenAngle * static_cast<float>(i);
            t[static_cast<std::size_t>(i + 1)] =
                glm::vec3(std::cos(theta) * ring, y, std::sin(theta) * ring);
        }
        return t;
    }();
    return table;
}

OcclusionMeasurement measureOcclusion(const PhysicsWorld& physics,
                                      const glm::vec3& listenerPos,
                                      const glm::vec3& sourcePos,
                                      int rayCount,
                                      float sourceRadius,
                                      JPH::BodyID ignoreBody)
{
    OcclusionMeasurement result;

    const int n = std::clamp(rayCount, 1, kMaxOcclusionRayCount);
    const std::array<glm::vec3, kMaxOcclusionRayCount>& offsets =
        occlusionRayOffsets();

    int blockedRays = 0;
    float nearestDistance = std::numeric_limits<float>::max();
    JPH::BodyID nearestBody;

    for (int i = 0; i < n; ++i)
    {
        const glm::vec3 target =
            sourcePos + offsets[static_cast<std::size_t>(i)] * sourceRadius;
        const glm::vec3 toTarget = target - listenerPos;
        const float distance     = glm::length(toTarget);

        // Coincident listener/target → zero-length direction. Skip this ray
        // (it counts toward the denominator as an un-blocked sample) rather
        // than normalise a zero vector. At rayCount==1 with a coincident
        // source this means the whole source reads unoccluded.
        if (distance <= 0.0f)
        {
            continue;
        }

        JPH::BodyID hitBody;
        float hitDistance = 0.0f;
        if (physics.rayCast(listenerPos, toTarget / distance, distance,
                            hitBody, hitDistance, ignoreBody))
        {
            ++blockedRays;
            if (hitDistance < nearestDistance)
            {
                nearestDistance = hitDistance;
                nearestBody     = hitBody;  // nearest wall along any sampled path
            }
        }
    }

    if (blockedRays > 0)
    {
        result.blocked        = true;
        result.targetFraction =
            static_cast<float>(blockedRays) / static_cast<float>(n);
        // Resolve the nearest blocking body's material (main-thread body lock).
        result.material = occlusionPresetForSurface(
            physics.getSurfaceMaterial(nearestBody));
    }
    return result;
}

namespace
{

/// @brief One flattened ray in the batch. A degenerate (zero-length) ray is
///        encoded as `maxDist == 0`, which `rayCast` reports as a miss — no
///        skip flag needed.
struct FlatRay
{
    glm::vec3 origin{0.0f};
    glm::vec3 dir{0.0f, 0.0f, 1.0f};  // placeholder for maxDist==0 rays
    float maxDist = 0.0f;
    JPH::BodyID ignoreBody;
};

struct RayHit
{
    bool blocked = false;
    JPH::BodyID body;
    float distance = 0.0f;
};

} // namespace

std::vector<OcclusionMeasurement> measureOcclusionBatch(
    const PhysicsWorld& physics, JobSystem& jobs,
    const std::vector<OcclusionQuery>& queries)
{
    std::vector<OcclusionMeasurement> out(queries.size());

    // --- main: flatten every query's rays into one array + record spans ---
    const std::array<glm::vec3, kMaxOcclusionRayCount>& offsets =
        occlusionRayOffsets();
    std::vector<FlatRay> rays;
    std::vector<std::size_t> spanBegin(queries.size());
    std::vector<int> spanCount(queries.size());
    for (std::size_t q = 0; q < queries.size(); ++q)
    {
        const OcclusionQuery& query = queries[q];
        const int n = std::clamp(query.rayCount, 1, kMaxOcclusionRayCount);
        spanBegin[q] = rays.size();
        spanCount[q] = n;
        for (int i = 0; i < n; ++i)
        {
            const glm::vec3 target =
                query.sourcePos
                + offsets[static_cast<std::size_t>(i)] * query.sourceRadius;
            const glm::vec3 toTarget = target - query.listenerPos;
            const float distance     = glm::length(toTarget);

            FlatRay ray;
            ray.origin     = query.listenerPos;
            ray.ignoreBody = query.ignoreBody;
            if (distance > 0.0f)
            {
                ray.dir     = toTarget / distance;
                ray.maxDist = distance;
            }
            // else: maxDist stays 0 → guaranteed miss (degenerate ray).
            rays.push_back(ray);
        }
    }

    // --- jobs: cast every ray on workers (read-only, no body lock held) ---
    std::vector<RayHit> hits(rays.size());
    const JobHandle handle = jobs.parallelFor(
        static_cast<std::uint32_t>(rays.size()),
        [&physics, &rays, &hits](std::uint32_t begin, std::uint32_t end)
        {
            for (std::uint32_t i = begin; i < end; ++i)
            {
                const FlatRay& ray = rays[i];
                JPH::BodyID body;
                float distance = 0.0f;
                if (physics.rayCast(ray.origin, ray.dir, ray.maxDist, body,
                                    distance, ray.ignoreBody))
                {
                    hits[i] = RayHit{true, body, distance};
                }
            }
        });
    jobs.wait(handle);

    // --- main: fold each span → fraction + nearest-blocker material ---
    for (std::size_t q = 0; q < queries.size(); ++q)
    {
        int blockedRays = 0;
        float nearestDistance = std::numeric_limits<float>::max();
        JPH::BodyID nearestBody;
        const std::size_t begin = spanBegin[q];
        for (int i = 0; i < spanCount[q]; ++i)
        {
            const RayHit& hit = hits[begin + static_cast<std::size_t>(i)];
            if (hit.blocked)
            {
                ++blockedRays;
                if (hit.distance < nearestDistance)
                {
                    nearestDistance = hit.distance;
                    nearestBody     = hit.body;
                }
            }
        }
        if (blockedRays > 0)
        {
            out[q].blocked        = true;
            out[q].targetFraction = static_cast<float>(blockedRays)
                                    / static_cast<float>(spanCount[q]);
            out[q].material = occlusionPresetForSurface(
                physics.getSurfaceMaterial(nearestBody));  // main-thread lock
        }
    }
    return out;
}

OcclusionServicingPlan planOcclusionServicing(int eligibleCount, int rayCount,
                                              int rayBudget, int cursor)
{
    OcclusionServicingPlan plan;
    if (eligibleCount <= 0)
    {
        return plan;  // nothing to service
    }

    const int perSource   = std::max(1, rayCount);
    const int serviceable = std::max(0, rayBudget / perSource);

    if (serviceable >= eligibleCount)
    {
        // Offered load fits — service everyone, reset the cursor.
        plan.serviced.reserve(static_cast<std::size_t>(eligibleCount));
        for (int i = 0; i < eligibleCount; ++i)
        {
            plan.serviced.push_back(i);
        }
        plan.nextCursor = 0;
        return plan;
    }

    // Over budget: service `serviceable` sources starting at the cursor,
    // wrapping. The rest are deferred; the cursor advances past this batch.
    plan.engaged = true;
    const int start = ((cursor % eligibleCount) + eligibleCount) % eligibleCount;
    plan.serviced.reserve(static_cast<std::size_t>(serviceable));
    for (int k = 0; k < serviceable; ++k)
    {
        plan.serviced.push_back((start + k) % eligibleCount);
    }
    plan.nextCursor = (start + serviceable) % eligibleCount;
    return plan;
}

float slewOcclusionFraction(float current, float target, float slewAmount)
{
    return current + (target - current) * slewAmount;
}

bool AudioOcclusionSystem::initialize(Engine& engine)
{
    m_engine = &engine;
    return true;
}

void AudioOcclusionSystem::shutdown()
{
    m_engine = nullptr;
}

void AudioOcclusionSystem::update(float deltaTime)
{
    if (m_engine == nullptr)
    {
        return;
    }

    PhysicsWorld& physics = m_engine->getPhysicsWorld();
    if (!physics.isInitialized())
    {
        return;  // No geometry to cast against — leave the fields untouched.
    }

    Scene* scene = m_engine->getSceneManager().getActiveScene();
    if (scene == nullptr)
    {
        return;
    }

    // The listener is the camera (single-listener engine). PostCamera phase
    // guarantees the camera has been stepped this frame.
    const glm::vec3 listenerPos = m_engine->getCamera().getPosition();
    const AudioMixer& mixer     = m_engine->getAudioMixer();

    // Per-frame slew fraction: how far occlusionFraction moves toward its
    // target this frame. clamp guards a long frame from overshooting.
    const float slew =
        std::clamp(deltaTime * kOcclusionSlewPerSec, 0.0f, 1.0f);

    // --- Pass 1: classify every source, build the eligible work list. ---
    // Every spatial source is slewed (Pass 4); only eligible ones get rays.
    struct Visited { std::uint32_t id; AudioSourceComponent* comp; };
    struct EligibleSource { std::uint32_t id; OcclusionQuery query; };
    std::vector<Visited> allSources;
    std::vector<EligibleSource> eligible;
    std::unordered_set<std::uint32_t> seen;

    scene->forEachEntity([&](Entity& entity)
    {
        auto* comp = entity.getComponent<AudioSourceComponent>();
        if (comp == nullptr)
        {
            return;
        }
        const std::uint32_t id = entity.getId();
        seen.insert(id);
        allSources.push_back({id, comp});

        // Eligible = spatial + audible (pre-compose gain above the floor) +
        // in range. A non-spatial / silent / distant source is CULLED: its
        // target is forced to 0 so the slew releases it (never frozen muffled).
        //
        // The design's "has a live voice this frame" clause is approximated by
        // the pre-compose gain estimate: this system runs before AudioSystem
        // and deliberately does NOT reach into its playback registry (keeping
        // occlusion decoupled from voice bookkeeping). A source that is audible
        // by the mixer + in range but not yet voiced only over-casts a few
        // rays — harmless, since nothing reads its occlusionFraction until it
        // plays. Uses the pure 3-arg resolveSourceGain (master × bus × volume,
        // no occlusion/ducking) — the composed gain doesn't exist yet here.
        bool eligibleNow = false;
        if (comp->spatial)
        {
            const glm::vec3 sourcePos = entity.getWorldPosition();
            const float distance = glm::length(sourcePos - listenerPos);
            const float preGain =
                resolveSourceGain(mixer, comp->bus, comp->volume);
            if (preGain > kAudibleGainEps
                && distance <= kDefaultOcclusionMaxDistance)
            {
                // A source with its own body must not occlude itself; a source
                // with no body casts with an invalid ignore id ({}).
                JPH::BodyID ignoreBody;
                if (auto* body = entity.getComponent<RigidBody>();
                    body != nullptr && body->hasBody())
                {
                    ignoreBody = body->getBodyId();
                }
                OcclusionQuery query;
                query.listenerPos  = listenerPos;
                query.sourcePos    = sourcePos;
                query.rayCount     = kDefaultOcclusionRayCount;
                query.sourceRadius = kDefaultOcclusionSourceRadius;
                query.ignoreBody   = ignoreBody;
                eligible.push_back({id, query});
                eligibleNow = true;
            }
        }
        if (!eligibleNow)
        {
            m_targets[id].fraction = 0.0f;  // culled → release (material held)
        }
    });

    // --- Pass 2: budget plan — which eligible sources get rays this frame. ---
    const OcclusionServicingPlan plan = planOcclusionServicing(
        static_cast<int>(eligible.size()), kDefaultOcclusionRayCount,
        kMaxOcclusionRaysPerFrame, m_roundRobinCursor);
    m_roundRobinCursor = plan.nextCursor;
    if (plan.engaged && !m_budgetEngagedLogged)
    {
        Logger::info("[AudioOcclusion] Ray budget ("
                     + std::to_string(kMaxOcclusionRaysPerFrame)
                     + " rays/frame) exceeded by " + std::to_string(eligible.size())
                     + " eligible sources — round-robin amortizing across frames");
        m_budgetEngagedLogged = true;
    }

    // --- Pass 3: cast the serviced sources' rays in parallel, fold results. ---
    // Serviced sources refresh their target; deferred sources hold theirs.
    std::vector<OcclusionQuery> queries;
    queries.reserve(plan.serviced.size());
    for (const int idx : plan.serviced)
    {
        queries.push_back(eligible[static_cast<std::size_t>(idx)].query);
    }
    const std::vector<OcclusionMeasurement> results =
        measureOcclusionBatch(physics, m_engine->getJobSystem(), queries);
    for (std::size_t k = 0; k < plan.serviced.size(); ++k)
    {
        const std::uint32_t id =
            eligible[static_cast<std::size_t>(plan.serviced[k])].id;
        OcclusionTarget& target = m_targets[id];
        target.fraction = results[k].targetFraction;
        if (results[k].blocked)
        {
            target.material = results[k].material;  // fresh + paired with fraction
        }
    }

    // --- Pass 4: slew EVERY source toward its target; snap material. ---
    // Running over all sources (not just serviced) is what releases a culled
    // source rather than freezing it muffled (§4.4).
    for (const Visited& v : allSources)
    {
        const OcclusionTarget& target = m_targets[v.id];
        v.comp->occlusionFraction = slewOcclusionFraction(
            v.comp->occlusionFraction, target.fraction, slew);
        // Material is only audible once fraction is audibly > 0, by which time
        // both have moved together; a target of 0 holds the last material.
        if (target.fraction > 0.0f)
        {
            v.comp->occlusionMaterial = target.material;
        }
    }

    // Reap targets for entities that left the scene this frame.
    for (auto it = m_targets.begin(); it != m_targets.end();)
    {
        it = (seen.find(it->first) == seen.end()) ? m_targets.erase(it)
                                                   : std::next(it);
    }
}

std::vector<uint32_t> AudioOcclusionSystem::getOwnedComponentTypes() const
{
    // Auto-activate whenever the scene has audio sources to occlude. Shares
    // ownership of the type with AudioSystem — ownership only drives
    // activation, so listing it in both systems is fine.
    return { ComponentTypeId::get<AudioSourceComponent>() };
}

} // namespace Vestige

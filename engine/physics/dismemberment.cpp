/// @file dismemberment.cpp
/// @brief Runtime mesh splitting implementation.

#include "physics/dismemberment.h"
#include "animation/skeleton.h"
#include "core/logger.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Vestige
{

SplitResult Dismemberment::splitAtBone(
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    const DismembermentZone& zone,
    const Skeleton& skeleton,
    const std::vector<glm::mat4>& boneMatrices)
{
    SplitResult result;

    if (vertices.empty() || indices.empty() || zone.boneIndex < 0)
    {
        result.success = false;
        return result;
    }

    // Classify each vertex as body-side (0) or limb-side (1)
    std::vector<int> side;
    classifyVertices(vertices, zone.boneIndex, skeleton, 0.1f, side);

    // Get cut plane in world space from bone transform
    glm::vec3 cutCenter(0.0f);
    glm::vec3 cutNormal = zone.cutPlaneNormal;

    if (static_cast<size_t>(zone.boneIndex) < boneMatrices.size())
    {
        // Transform cut plane by bone's world matrix
        // The bone matrix includes inverseBindMatrix, so we need the raw global transform
        glm::mat4 boneMat = boneMatrices[static_cast<size_t>(zone.boneIndex)];
        cutCenter = glm::vec3(boneMat[3]);
        cutNormal = glm::normalize(glm::vec3(boneMat * glm::vec4(zone.cutPlaneNormal, 0.0f)));
    }

    // Track vertex remapping for body and limb meshes
    std::vector<int> bodyVertMap(vertices.size(), -1);
    std::vector<int> limbVertMap(vertices.size(), -1);

    // Boundary edge points for cap generation
    std::vector<glm::vec3> edgePoints;

    // Process each triangle
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            continue;

        int s0 = side[i0];
        int s1 = side[i1];
        int s2 = side[i2];

        // All on body side
        if (s0 == 0 && s1 == 0 && s2 == 0)
        {
            for (uint32_t idx : {i0, i1, i2})
            {
                if (bodyVertMap[idx] < 0)
                {
                    bodyVertMap[idx] = static_cast<int>(result.bodyVertices.size());
                    result.bodyVertices.push_back(vertices[idx]);
                }
                result.bodyIndices.push_back(static_cast<uint32_t>(bodyVertMap[idx]));
            }
            continue;
        }

        // All on limb side
        if (s0 == 1 && s1 == 1 && s2 == 1)
        {
            for (uint32_t idx : {i0, i1, i2})
            {
                if (limbVertMap[idx] < 0)
                {
                    limbVertMap[idx] = static_cast<int>(result.limbVertices.size());
                    result.limbVertices.push_back(vertices[idx]);
                }
                result.limbIndices.push_back(static_cast<uint32_t>(limbVertMap[idx]));
            }
            continue;
        }

        // Triangle straddles the boundary — split it
        // Identify which vertex is the "odd one out"
        uint32_t triIdx[3] = {i0, i1, i2};
        int triSide[3] = {s0, s1, s2};

        for (int vi = 0; vi < 3; ++vi)
        {
            int next = (vi + 1) % 3;

            if (triSide[vi] != triSide[next])
            {
                // Edge crosses boundary — compute split vertex
                const Vertex& va = vertices[triIdx[vi]];
                const Vertex& vb = vertices[triIdx[next]];

                // Find the split point using the cut plane
                float da = glm::dot(va.position - cutCenter, cutNormal);
                float db = glm::dot(vb.position - cutCenter, cutNormal);
                float denom = da - db;

                float t = 0.5f;
                if (std::abs(denom) > 0.0001f)
                {
                    t = da / denom;
                    t = glm::clamp(t, 0.0f, 1.0f);
                }

                Vertex splitVert = interpolateVertex(va, vb, t);
                edgePoints.push_back(splitVert.position);
            }
        }

        // Simplified: assign the whole straddling triangle to the majority side
        int bodySideCount = (s0 == 0 ? 1 : 0) + (s1 == 0 ? 1 : 0) + (s2 == 0 ? 1 : 0);

        if (bodySideCount >= 2)
        {
            // Assign to body mesh
            for (uint32_t idx : {i0, i1, i2})
            {
                if (bodyVertMap[idx] < 0)
                {
                    bodyVertMap[idx] = static_cast<int>(result.bodyVertices.size());
                    result.bodyVertices.push_back(vertices[idx]);
                }
                result.bodyIndices.push_back(static_cast<uint32_t>(bodyVertMap[idx]));
            }
        }
        else
        {
            // Assign to limb mesh
            for (uint32_t idx : {i0, i1, i2})
            {
                if (limbVertMap[idx] < 0)
                {
                    limbVertMap[idx] = static_cast<int>(result.limbVertices.size());
                    result.limbVertices.push_back(vertices[idx]);
                }
                result.limbIndices.push_back(static_cast<uint32_t>(limbVertMap[idx]));
            }
        }
    }

    // Generate cap mesh from boundary edge points
    if (!edgePoints.empty())
    {
        generateCapMesh(edgePoints, cutNormal, cutCenter,
                        result.capVertices, result.capIndices);
    }

    // Compute limb centroid and volume
    if (!result.limbVertices.empty())
    {
        glm::vec3 sum(0.0f);
        for (const auto& v : result.limbVertices)
        {
            sum += v.position;
        }
        result.limbCentroid = sum / static_cast<float>(result.limbVertices.size());

        // Approximate volume from bounding box
        glm::vec3 bMin(FLT_MAX), bMax(-FLT_MAX);
        for (const auto& v : result.limbVertices)
        {
            bMin = glm::min(bMin, v.position);
            bMax = glm::max(bMax, v.position);
        }
        glm::vec3 size = bMax - bMin;
        result.limbVolume = size.x * size.y * size.z;
    }

    result.success = !result.bodyVertices.empty() && !result.limbVertices.empty();
    return result;
}

void Dismemberment::classifyVertices(
    const std::vector<Vertex>& vertices,
    int cutBoneIndex,
    const Skeleton& skeleton,
    float weightThreshold,
    std::vector<int>& outSide)
{
    outSide.resize(vertices.size(), 0);

    // Build set of bones that are part of the severed limb
    // (the cut bone and all its descendants)
    std::unordered_set<int> limbBones;
    limbBones.insert(cutBoneIndex);

    // Find all descendants
    int jointCount = skeleton.getJointCount();
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (int i = 0; i < jointCount; ++i)
        {
            if (limbBones.count(i))
                continue;

            int parent = skeleton.m_joints[static_cast<size_t>(i)].parentIndex;
            if (parent >= 0 && limbBones.count(parent))
            {
                limbBones.insert(i);
                changed = true;
            }
        }
    }

    // Classify each vertex based on bone weight dominance
    for (size_t vi = 0; vi < vertices.size(); ++vi)
    {
        const auto& v = vertices[vi];
        float limbWeight = 0.0f;
        float bodyWeight = 0.0f;

        for (int bi = 0; bi < 4; ++bi)
        {
            int boneIdx = v.boneIds[bi];
            float weight = v.boneWeights[bi];

            if (weight < weightThreshold)
                continue;

            if (boneIdx < 0 || boneIdx >= jointCount)
                continue;

            if (limbBones.count(boneIdx))
            {
                limbWeight += weight;
            }
            else
            {
                bodyWeight += weight;
            }
        }

        outSide[vi] = (limbWeight > bodyWeight) ? 1 : 0;
    }
}

Vertex Dismemberment::interpolateVertex(const Vertex& a, const Vertex& b, float t)
{
    Vertex result;
    result.position = glm::mix(a.position, b.position, t);
    result.color = glm::mix(a.color, b.color, t);
    result.texCoord = glm::mix(a.texCoord, b.texCoord, t);

    // Safe normalize for interpolated direction vectors (opposite directions can mix to zero)
    auto safeNormalize = [](const glm::vec3& v) -> glm::vec3
    {
        float len = glm::length(v);
        return (len > 0.0001f) ? (v / len) : glm::vec3(0.0f, 1.0f, 0.0f);
    };
    result.normal = safeNormalize(glm::mix(a.normal, b.normal, t));
    result.tangent = safeNormalize(glm::mix(a.tangent, b.tangent, t));
    result.bitangent = safeNormalize(glm::mix(a.bitangent, b.bitangent, t));

    // Interpolate bone weights — take the dominant side
    if (t < 0.5f)
    {
        result.boneIds = a.boneIds;
        result.boneWeights = a.boneWeights;
    }
    else
    {
        result.boneIds = b.boneIds;
        result.boneWeights = b.boneWeights;
    }

    return result;
}

void Dismemberment::generateCapMesh(
    const std::vector<glm::vec3>& edgePoints,
    const glm::vec3& cutPlaneNormal,
    const glm::vec3& /*cutPlaneCenter*/,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    if (edgePoints.size() < 3)
        return;

    // Project edge points onto the cut plane and sort by angle
    float normalLen = glm::length(cutPlaneNormal);
    if (normalLen < 0.0001f)
        return;
    glm::vec3 normal = cutPlaneNormal / normalLen;

    // Build local 2D frame on the cut plane
    glm::vec3 tangent;
    if (std::abs(normal.y) < 0.9f)
        tangent = glm::normalize(glm::cross(normal, glm::vec3(0, 1, 0)));
    else
        tangent = glm::normalize(glm::cross(normal, glm::vec3(1, 0, 0)));
    glm::vec3 bitangent = glm::cross(normal, tangent);

    // Compute centroid of edge points
    glm::vec3 center(0.0f);
    for (const auto& p : edgePoints)
    {
        center += p;
    }
    center /= static_cast<float>(edgePoints.size());

    // Sort points by angle
    struct AnglePoint
    {
        glm::vec3 pos;
        float angle;
    };

    std::vector<AnglePoint> sortedPoints;
    sortedPoints.reserve(edgePoints.size());
    for (const auto& p : edgePoints)
    {
        glm::vec3 d = p - center;
        float angle = std::atan2(glm::dot(d, bitangent), glm::dot(d, tangent));
        sortedPoints.push_back({p, angle});
    }

    std::sort(sortedPoints.begin(), sortedPoints.end(),
              [](const AnglePoint& a, const AnglePoint& b)
              { return a.angle < b.angle; });

    // Remove near-duplicate points
    std::vector<glm::vec3> uniquePoints;
    uniquePoints.reserve(sortedPoints.size());
    for (const auto& sp : sortedPoints)
    {
        bool duplicate = false;
        for (const auto& existing : uniquePoints)
        {
            if (glm::distance(existing, sp.pos) < 0.001f)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
        {
            uniquePoints.push_back(sp.pos);
        }
    }

    if (uniquePoints.size() < 3)
        return;

    // Fan triangulation from center
    auto baseIdx = static_cast<uint32_t>(outVertices.size());

    // Add center vertex
    Vertex centerVert;
    centerVert.position = center;
    centerVert.normal = normal;
    centerVert.texCoord = glm::vec2(0.5f, 0.5f);
    outVertices.push_back(centerVert);

    // Add edge vertices
    for (const auto& p : uniquePoints)
    {
        Vertex v;
        v.position = p;
        v.normal = normal;
        // UV from planar projection
        glm::vec3 d = p - center;
        v.texCoord = glm::vec2(glm::dot(d, tangent), glm::dot(d, bitangent));
        outVertices.push_back(v);
    }

    // Fan triangles
    for (size_t i = 0; i < uniquePoints.size(); ++i)
    {
        size_t next = (i + 1) % uniquePoints.size();
        outIndices.push_back(baseIdx);
        outIndices.push_back(baseIdx + 1 + static_cast<uint32_t>(i));
        outIndices.push_back(baseIdx + 1 + static_cast<uint32_t>(next));
    }
}

} // namespace Vestige

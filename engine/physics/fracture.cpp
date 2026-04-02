/// @file fracture.cpp
/// @brief Voronoi fracture algorithm implementation.

#include "physics/fracture.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace Vestige
{

FractureResult Fracture::fractureConvex(
    const std::vector<glm::vec3>& vertices,
    const std::vector<uint32_t>& indices,
    int fragmentCount,
    const glm::vec3& impactPoint,
    uint32_t seed)
{
    FractureResult result;

    if (vertices.empty() || indices.empty() || fragmentCount < 2)
    {
        result.success = false;
        return result;
    }

    // Compute bounding box
    AABB bounds;
    bounds.min = glm::vec3(std::numeric_limits<float>::max());
    bounds.max = glm::vec3(std::numeric_limits<float>::lowest());
    for (const auto& v : vertices)
    {
        bounds.min = glm::min(bounds.min, v);
        bounds.max = glm::max(bounds.max, v);
    }

    // Generate Voronoi seed points
    auto seeds = generateSeeds(bounds, fragmentCount, impactPoint, seed);

    if (static_cast<int>(seeds.size()) < 2)
    {
        result.success = false;
        return result;
    }

    // Collect all original mesh vertices as the starting convex hull
    // For each Voronoi cell (seed point), clip the mesh bounding box
    // by the bisector planes between this seed and all other seeds
    for (size_t si = 0; si < seeds.size(); ++si)
    {
        const glm::vec3& cellSeed = seeds[si];

        // Start with the bounding box vertices as the convex polytope
        glm::vec3 bMin = bounds.min;
        glm::vec3 bMax = bounds.max;

        // Slight expansion to avoid edge cases
        bMin -= glm::vec3(0.001f);
        bMax += glm::vec3(0.001f);

        std::vector<glm::vec3> cellVertices = {
            {bMin.x, bMin.y, bMin.z}, {bMax.x, bMin.y, bMin.z},
            {bMin.x, bMax.y, bMin.z}, {bMax.x, bMax.y, bMin.z},
            {bMin.x, bMin.y, bMax.z}, {bMax.x, bMin.y, bMax.z},
            {bMin.x, bMax.y, bMax.z}, {bMax.x, bMax.y, bMax.z}
        };

        // Clip by bisector planes with all other seeds
        bool valid = true;
        for (size_t sj = 0; sj < seeds.size(); ++sj)
        {
            if (si == sj)
                continue;

            // Bisector plane: midpoint between seeds, normal pointing toward cellSeed
            glm::vec3 midpoint = 0.5f * (cellSeed + seeds[sj]);
            glm::vec3 normal = glm::normalize(cellSeed - seeds[sj]);

            cellVertices = clipConvexByPlane(cellVertices, midpoint, normal);

            if (cellVertices.size() < 4)
            {
                valid = false;
                break;
            }
        }

        if (!valid || cellVertices.size() < 4)
            continue;

        // Also clip by the original mesh bounding box faces
        // (already done implicitly since we started from the AABB)

        // Build the fragment mesh from the clipped convex cell
        FractureFragment fragment;
        fragment.centroid = computeCentroid(cellVertices);

        // Build convex hull faces from the cell vertices
        buildConvexHullMesh(cellVertices, fragment, cellVertices);

        // Compute volume
        fragment.volume = std::abs(computeVolume(fragment.positions, fragment.indices));

        if (fragment.volume > 0.0001f && !fragment.positions.empty())
        {
            result.fragments.push_back(std::move(fragment));
        }
    }

    result.success = !result.fragments.empty();
    return result;
}

std::vector<glm::vec3> Fracture::generateSeeds(
    const AABB& bounds,
    int count,
    const glm::vec3& impactBias,
    uint32_t seed)
{
    std::mt19937 rng(seed);
    std::vector<glm::vec3> seeds;
    seeds.reserve(static_cast<size_t>(count));

    glm::vec3 bMin = bounds.min;
    glm::vec3 bMax = bounds.max;
    glm::vec3 bSize = bMax - bMin;

    if (bSize.x < 0.001f || bSize.y < 0.001f || bSize.z < 0.001f)
        return seeds;

    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    // Bias distribution: 60% of seeds near impact, 40% uniform
    int biasedCount = count * 3 / 5;
    int uniformCount = count - biasedCount;

    // Clamp impact point to bounds
    glm::vec3 clampedImpact = glm::clamp(impactBias, bMin, bMax);

    // Biased seeds: Gaussian around impact point
    float sigma = glm::length(bSize) * 0.25f;
    std::normal_distribution<float> gaussDist(0.0f, sigma);

    for (int i = 0; i < biasedCount; ++i)
    {
        glm::vec3 offset(gaussDist(rng), gaussDist(rng), gaussDist(rng));
        glm::vec3 point = glm::clamp(clampedImpact + offset, bMin, bMax);
        seeds.push_back(point);
    }

    // Uniform seeds
    for (int i = 0; i < uniformCount; ++i)
    {
        glm::vec3 point(
            bMin.x + dist(rng) * bSize.x,
            bMin.y + dist(rng) * bSize.y,
            bMin.z + dist(rng) * bSize.z
        );
        seeds.push_back(point);
    }

    return seeds;
}

float Fracture::computeVolume(
    const std::vector<glm::vec3>& positions,
    const std::vector<uint32_t>& indices)
{
    float volume = 0.0f;

    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const glm::vec3& v0 = positions[indices[i]];
        const glm::vec3& v1 = positions[indices[i + 1]];
        const glm::vec3& v2 = positions[indices[i + 2]];

        // Divergence theorem: V = (1/6) * sum(dot(v0, cross(v1, v2)))
        volume += glm::dot(v0, glm::cross(v1, v2));
    }

    return volume / 6.0f;
}

glm::vec3 Fracture::computeCentroid(const std::vector<glm::vec3>& points)
{
    if (points.empty())
        return glm::vec3(0.0f);

    glm::vec3 sum(0.0f);
    for (const auto& p : points)
    {
        sum += p;
    }
    return sum / static_cast<float>(points.size());
}

std::vector<glm::vec3> Fracture::clipConvexByPlane(
    const std::vector<glm::vec3>& vertices,
    const glm::vec3& planePoint,
    const glm::vec3& planeNormal)
{
    if (vertices.empty())
        return {};

    // Classify each vertex as inside (positive) or outside (negative)
    std::vector<float> distances;
    distances.reserve(vertices.size());
    for (const auto& v : vertices)
    {
        distances.push_back(glm::dot(v - planePoint, planeNormal));
    }

    // Collect vertices on the positive side
    std::vector<glm::vec3> result;
    result.reserve(vertices.size());

    for (size_t i = 0; i < vertices.size(); ++i)
    {
        if (distances[i] >= -0.0001f)
        {
            result.push_back(vertices[i]);
        }
    }

    // For edges that cross the plane, compute intersection points
    // We need to check all pairs of vertices that form edges of the convex hull
    // For simplicity with a point cloud, we check all pairs
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        for (size_t j = i + 1; j < vertices.size(); ++j)
        {
            float di = distances[i];
            float dj = distances[j];

            // Edge crosses the plane
            if ((di > 0.0001f && dj < -0.0001f) || (di < -0.0001f && dj > 0.0001f))
            {
                float t = di / (di - dj);
                glm::vec3 intersection = vertices[i] + t * (vertices[j] - vertices[i]);
                result.push_back(intersection);
            }
        }
    }

    return result;
}

void Fracture::triangulateFace(
    const std::vector<glm::vec3>& polygon,
    const glm::vec3& faceNormal,
    std::vector<glm::vec3>& outPositions,
    std::vector<glm::vec3>& outNormals,
    std::vector<glm::vec2>& outUvs,
    std::vector<uint32_t>& outIndices,
    bool isInterior,
    std::vector<bool>& outInterior)
{
    if (polygon.size() < 3)
        return;

    auto baseIdx = static_cast<uint32_t>(outPositions.size());

    // Add vertices
    for (const auto& p : polygon)
    {
        outPositions.push_back(p);
        outNormals.push_back(faceNormal);

        // Planar UV projection
        // Build a local 2D frame on the face
        glm::vec3 tangent;
        if (std::abs(faceNormal.y) < 0.9f)
            tangent = glm::normalize(glm::cross(faceNormal, glm::vec3(0, 1, 0)));
        else
            tangent = glm::normalize(glm::cross(faceNormal, glm::vec3(1, 0, 0)));
        glm::vec3 bitangent = glm::cross(faceNormal, tangent);

        outUvs.emplace_back(glm::dot(p, tangent), glm::dot(p, bitangent));
    }

    // Fan triangulation
    for (size_t i = 1; i + 1 < polygon.size(); ++i)
    {
        outIndices.push_back(baseIdx);
        outIndices.push_back(baseIdx + static_cast<uint32_t>(i));
        outIndices.push_back(baseIdx + static_cast<uint32_t>(i + 1));

        outInterior.push_back(isInterior);
        outInterior.push_back(isInterior);
        outInterior.push_back(isInterior);
    }
}

void Fracture::buildConvexHullMesh(
    const std::vector<glm::vec3>& hullPoints,
    FractureFragment& outFragment,
    const std::vector<glm::vec3>& /*cellVertices*/)
{
    if (hullPoints.size() < 4)
        return;

    glm::vec3 center = computeCentroid(hullPoints);

    // Simple approach: for each axis-aligned direction pair, find the
    // extreme support points and build faces from them.
    // For a convex point cloud from Voronoi clipping, we know each face
    // came from a bisector plane. Build faces by grouping coplanar points.

    // Collect unique face normals by testing pairs of edges
    struct Face
    {
        glm::vec3 normal;
        std::vector<glm::vec3> vertices;
    };
    std::vector<Face> faces;

    // Limit search to avoid O(n^3): sample a subset if too many points
    size_t maxPoints = std::min(hullPoints.size(), size_t(30));

    for (size_t i = 0; i < maxPoints; ++i)
    {
        for (size_t j = i + 1; j < maxPoints; ++j)
        {
            for (size_t k = j + 1; k < maxPoints; ++k)
            {
                glm::vec3 edge1 = hullPoints[j] - hullPoints[i];
                glm::vec3 edge2 = hullPoints[k] - hullPoints[i];
                glm::vec3 normal = glm::cross(edge1, edge2);

                float normalLen = glm::length(normal);
                if (normalLen < 0.00001f)
                    continue;
                normal /= normalLen;

                // Ensure outward-facing
                if (glm::dot(normal, hullPoints[i] - center) < 0.0f)
                    normal = -normal;

                // Check this is a valid hull face (all points on negative side)
                bool valid = true;
                float planeD = glm::dot(normal, hullPoints[i]);
                for (size_t m = 0; m < hullPoints.size(); ++m)
                {
                    if (m == i || m == j || m == k) continue;
                    if (glm::dot(normal, hullPoints[m]) - planeD > 0.001f)
                    {
                        valid = false;
                        break;
                    }
                }
                if (!valid) continue;

                // Deduplicate by normal direction
                bool dup = false;
                for (auto& f : faces)
                {
                    if (glm::dot(f.normal, normal) > 0.999f)
                    {
                        dup = true;
                        break;
                    }
                }
                if (dup) continue;

                // Collect all coplanar points
                Face face;
                face.normal = normal;
                for (size_t m = 0; m < hullPoints.size(); ++m)
                {
                    float dist = std::abs(glm::dot(normal, hullPoints[m]) - planeD);
                    if (dist < 0.001f)
                        face.vertices.push_back(hullPoints[m]);
                }
                if (face.vertices.size() >= 3)
                    faces.push_back(std::move(face));
            }
        }
    }

    // Sort face vertices and triangulate
    for (auto& face : faces)
    {
        glm::vec3 faceCenter = computeCentroid(face.vertices);

        glm::vec3 tangent;
        if (std::abs(face.normal.y) < 0.9f)
            tangent = glm::normalize(glm::cross(face.normal, glm::vec3(0, 1, 0)));
        else
            tangent = glm::normalize(glm::cross(face.normal, glm::vec3(1, 0, 0)));
        glm::vec3 bitangent = glm::cross(face.normal, tangent);

        std::sort(face.vertices.begin(), face.vertices.end(),
            [&](const glm::vec3& a, const glm::vec3& b)
            {
                glm::vec3 da = a - faceCenter;
                glm::vec3 db = b - faceCenter;
                return std::atan2(glm::dot(da, bitangent), glm::dot(da, tangent))
                     < std::atan2(glm::dot(db, bitangent), glm::dot(db, tangent));
            });

        triangulateFace(face.vertices, face.normal,
                        outFragment.positions, outFragment.normals,
                        outFragment.uvs, outFragment.indices,
                        true, outFragment.isInteriorFace);
    }
}

} // namespace Vestige
